//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      (cacowards browser feature)
// Description: tab showing an expandable, checkable list of Cacowards-awarded WADs, downloadable from /idgames
//======================================================================================================================

#include "CacowardsTab.hpp"

#include "Utils/ZipReader.hpp"   // extractZipArchive
#include "Utils/FileSystemUtils.hpp"  // isDirectoryWritable

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDate>
#include <QDesktopServices>
#include <QTime>
#include <QXmlStreamReader>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QtGlobal>  // qBound, qMax, qint64
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>    // std::make_unique

#include <algorithm>
#include <random>


//======================================================================================================================
// constants

namespace {

constexpr const char * kUserAgent = "DoomRunner/2.0 (cacowards browser)";

constexpr const char * kIdgamesApiBase = "https://www.doomworld.com/idgames/api/api.php";
constexpr const char * kDoomwikiIndex = "https://doomwiki.org/w/index.php";

/// Wayback Machine URL prefix used to fetch a doomwiki Cacowards year page without tripping Cloudflare.
constexpr const char * kWaybackYearPrefix = "https://web.archive.org/web/2025/https://doomwiki.org/wiki/Cacowards_";

/// Download mirrors of the /idgames archive, tried in order.
/** The primary doomworld.com URL 301-redirects to legacy.doomworld.com whose TLS
  * certificate has expired, so we use direct mirrors that serve the raw files instead. */
constexpr const char * kDownloadMirrors [] = {
	"https://youfailit.net/pub/idgames/",
	"https://www.gamers.org/pub/idgames/",
	"https://ftp.gamers.org/pub/idgames/",
	"https://ftp.fu-berlin.de/pc/games/idgames/",
	"https://idgames.planetzdoom.com/",
	"https://files.xvertigox.com/idgames/",
	"https://mirror.braindrainlan.nu/pub/idgames/",
	"https://lethe.chinstrap.org/idgames/",
	"https://mirrors.lug.mtu.edu/idgames/",
	"https://ftpmirror1.infania.net/pub/idgames/",
};

/// Returns the mirror prefixes in a random order, so parallel downloads don't all hit the same mirror.
QStringList shuffledMirrors()
{
	QStringList bases;
	bases.reserve( int( std::size(kDownloadMirrors) ) );
	for (const char * base : kDownloadMirrors)
		bases.append( QString::fromLatin1( base ) );

	std::random_device rd;
	std::mt19937 rng( rd() );
	std::shuffle( bases.begin(), bases.end(), rng );
	return bases;
}

/// Maps a doomwiki section heading to one of the supported award categories.
/** Returns an empty string for headings that do not represent an award section. */
QString classifyCategory( const QString & heading )
{
	const QString c = heading.toLower();
	if (c.contains( "winner" ))
		return QStringLiteral("Winners");
	if (c.contains( "runner" ))
		return QStringLiteral("Runners-Up");
	if (c.contains( "honor" ) || c.contains( "honour" ) || c.contains( "mention" ))
		return QStringLiteral("Honorable Mention");
	if (c.contains( "best" ) || c.contains( "worst" ) || c.contains( "award" ) || c.contains( "mordeth" )
		|| c.contains( "mockaward" ) || c.contains( "mapper" ) || c.contains( "lifetime" )
		|| c.contains( "promising" ) || c.contains( "newcomer" ) || c.contains( "espi" )
		|| c.contains( "multiplayer" ) || c.contains( "most" ) || c.contains( "special" ) || c.contains( "achievement" ))
		return QStringLiteral("Other Awards");
	return QString();
}

int categoryRank( const QString & category )
{
	if (category == QLatin1String("Winners"))           return 0;
	if (category == QLatin1String("Runners-Up"))        return 1;
	if (category == QLatin1String("Honorable Mention")) return 2;
	return 3;
}

} // namespace


//======================================================================================================================

CacowardsTab::CacowardsTab( QWidget * parent )
	: QWidget( parent ),
	  network_( new QNetworkAccessManager( this ) )
{
	buildUi();
}

CacowardsTab::~CacowardsTab()
{
	for (const auto & download : activeDownloads_)
	{
		if (download->reply)
			download->reply->abort();
		if (download->resolveReply)
			download->resolveReply->abort();
		delete download->file;
	}
	if (refreshReply_)
		refreshReply_->abort();
}

void CacowardsTab::setTargetDir( const QString & dir )
{
	targetDirLine_->setText( dir );
}

QString CacowardsTab::targetDir() const
{
	return targetDirLine_->text().trimmed();
}

void CacowardsTab::setAutosort( bool enabled )
{
	autosortChk_->setChecked( enabled );
}

bool CacowardsTab::autosort() const
{
	return autosortChk_->isChecked();
}

void CacowardsTab::setUnpack( bool enabled )
{
	unpackChk_->setChecked( enabled );
	deleteAfterExtractChk_->setEnabled( enabled );
}

bool CacowardsTab::unpack() const
{
	return unpackChk_->isChecked();
}

void CacowardsTab::setDeleteAfterExtract( bool enabled )
{
	deleteAfterExtractChk_->setChecked( enabled );
}

bool CacowardsTab::deleteAfterExtract() const
{
	return deleteAfterExtractChk_->isChecked();
}

void CacowardsTab::setDataFilePath( const QString & path )
{
	dataFilePath_ = path;
	loadData();
}

QStringList CacowardsTab::expandedNodes() const
{
	QStringList nodes;

	QTreeWidgetItemIterator it( tree_ );
	while (*it)
	{
		QTreeWidgetItem * node = *it;
		if (node->isExpanded())
		{
			QStringList path;
			for (QTreeWidgetItem * cur = node; cur; cur = cur->parent())
				path.prepend( cur->text( 0 ) );
			nodes.append( path.join( '/' ) );
		}
		++it;
	}

	return nodes;
}

void CacowardsTab::setExpandedNodes( const QStringList & nodes )
{
	expandedNodes_ = nodes;
}

void CacowardsTab::setParallelDownload( bool enabled )
{
	maxParallel_ = enabled ? 4 : 1;
	if (parallelChk_)
		parallelChk_->setChecked( enabled );
}

bool CacowardsTab::parallelDownload() const
{
	return maxParallel_ > 1;
}

void CacowardsTab::setMapSourceDir( const QString & dir )
{
	mapSourceDir_ = dir;
}

//----------------------------------------------------------------------------------------------------------------------

void CacowardsTab::buildUi()
{
	//-- tree + refresh button --------------------------------------------------

	refreshBtn_ = new QPushButton( "Refresh", this );
	refreshBtn_->setToolTip( "Re-download the Cacowards list automatically (via the Wayback Machine) and save it to disk" );

	generateListBtn_ = new QPushButton( "Generate list", this );
	generateListBtn_->setToolTip( "Open doomwiki's export page in your browser so you can download the Cacowards XML yourself" );

	importBtn_ = new QPushButton( "Import...", this );
	importBtn_->setToolTip( "Load the Cacowards list from a doomwiki export XML file you downloaded in your browser" );

	QHBoxLayout * refreshRow = new QHBoxLayout;
	refreshRow->addStretch( 1 );
	refreshRow->addWidget( refreshBtn_ );
	refreshRow->addWidget( generateListBtn_ );
	refreshRow->addWidget( importBtn_ );

	tree_ = new QTreeWidget( this );
	tree_->setColumnCount( 1 );
	tree_->setHeaderLabel( "WAD" );
	tree_->setSelectionMode( QAbstractItemView::SingleSelection );

	detailsView_ = new QTextEdit( this );
	detailsView_->setReadOnly( true );
	detailsView_->setPlaceholderText( "Select an entry to see its details." );
	detailsView_->setMinimumHeight( 100 );

	QWidget * treePanel = new QWidget( this );
	QVBoxLayout * treeLayout = new QVBoxLayout( treePanel );
	treeLayout->setContentsMargins( 0, 0, 0, 0 );
	treeLayout->addLayout( refreshRow );
	treeLayout->addWidget( tree_, 1 );
	treeLayout->addWidget( detailsView_ );

	//-- bottom row: target dir + download -------------------------------------

	targetDirLine_ = new QLineEdit( this );
	targetDirLine_->setPlaceholderText( "Directory to save downloads into" );

	browseBtn_ = new QPushButton( "Browse...", this );

	downloadBtn_ = new QPushButton( "Download", this );
	downloadBtn_->setToolTip( "Download all checked entries" );
	downloadBtn_->setEnabled( false );

	QHBoxLayout * downloadRow = new QHBoxLayout;
	downloadRow->addWidget( new QLabel( "Target directory:", this ) );
	downloadRow->addWidget( targetDirLine_, 1 );
	downloadRow->addWidget( browseBtn_ );
	downloadRow->addSpacing( 10 );
	downloadRow->addWidget( downloadBtn_ );

	//-- download options -------------------------------------------------------

	autosortChk_ = new QCheckBox( "Autosort", this );
	autosortChk_->setToolTip( "Save downloads into \"Cacowards/[Year]/[Category]\" subfolders" );

	unpackChk_ = new QCheckBox( "Unpack", this );
	unpackChk_->setToolTip( "Extract each downloaded ZIP into a folder named after the archive" );

	deleteAfterExtractChk_ = new QCheckBox( "Delete after extracting", this );
	deleteAfterExtractChk_->setToolTip( "Delete the original archive after it has been unpacked" );
	deleteAfterExtractChk_->setEnabled( false );

	parallelChk_ = new QCheckBox( "Download 4 at a time", this );
	parallelChk_->setToolTip( "Download up to 4 files at the same time (otherwise they are downloaded one by one)" );
	parallelChk_->setChecked( maxParallel_ > 1 );

	QHBoxLayout * optionsRow = new QHBoxLayout;
	optionsRow->addStretch( 1 );
	optionsRow->addWidget( autosortChk_ );
	optionsRow->addWidget( unpackChk_ );
	optionsRow->addWidget( deleteAfterExtractChk_ );
	optionsRow->addWidget( parallelChk_ );

	//-- activity log -----------------------------------------------------------

	logChk_ = new QCheckBox( "Show log", this );
	logChk_->setToolTip( "Show a log of list loading/importing, downloads and extraction events" );

	logView_ = new QPlainTextEdit( this );
	logView_->setReadOnly( true );
	logView_->setPlaceholderText( "No activity logged yet." );
	logView_->setMinimumHeight( 140 );
	logView_->setVisible( false );  // hidden until the "Show log" checkbox is enabled

	QHBoxLayout * logRow = new QHBoxLayout;
	logRow->addStretch( 1 );
	logRow->addWidget( logChk_ );

	//-- status bar -------------------------------------------------------------

	statusLabel_ = new QLabel( "Loading Cacowards data...", this );
	statusLabel_->setWordWrap( true );

	progressBar_ = new QProgressBar( this );
	progressBar_->setVisible( false );
	progressBar_->setRange( 0, 0 );  // busy indicator by default

	//-- assemble ---------------------------------------------------------------

	QVBoxLayout * mainLayout = new QVBoxLayout( this );
	mainLayout->addWidget( treePanel, 1 );
	mainLayout->addLayout( downloadRow );
	mainLayout->addLayout( optionsRow );
	mainLayout->addLayout( logRow );
	mainLayout->addWidget( logView_ );
	mainLayout->addWidget( progressBar_ );
	mainLayout->addWidget( statusLabel_ );

	//-- signal/slot wiring -----------------------------------------------------

	connect( refreshBtn_, &QPushButton::clicked, this, &CacowardsTab::refresh );
	connect( generateListBtn_, &QPushButton::clicked, this, &CacowardsTab::generateList );
	connect( importBtn_, &QPushButton::clicked, this, &CacowardsTab::importExportedXml );
	connect( tree_, &QTreeWidget::currentItemChanged, this, &CacowardsTab::onCurrentItemChanged );
	connect( tree_, &QTreeWidget::itemChanged, this, &CacowardsTab::onItemChanged );
	connect( tree_, &QTreeWidget::itemExpanded, this, [ this ]( QTreeWidgetItem * ){ onExpansionChanged(); } );
	connect( tree_, &QTreeWidget::itemCollapsed, this, [ this ]( QTreeWidgetItem * ){ onExpansionChanged(); } );
	connect( browseBtn_, &QPushButton::clicked, this, &CacowardsTab::browseTargetDir );
	connect( downloadBtn_, &QPushButton::clicked, this, &CacowardsTab::downloadChecked );

	// save the target directory as soon as the user changes it
	connect( targetDirLine_, &QLineEdit::textChanged, this, [ this ]( const QString & text )
	{
		emit targetDirChanged( text.trimmed() );
	});

	connect( autosortChk_, &QCheckBox::toggled, this, &CacowardsTab::autosortChanged );
	connect( unpackChk_, &QCheckBox::toggled, this, &CacowardsTab::unpackChanged );
	connect( deleteAfterExtractChk_, &QCheckBox::toggled, this, &CacowardsTab::deleteAfterExtractChanged );
	connect( parallelChk_, &QCheckBox::toggled, this, [ this ]( bool enabled )
	{
		setParallelDownload( enabled );  // maxParallel_ = enabled ? 4 : 1
		emit parallelDownloadChanged( enabled );
	});

	// "delete after extracting" only makes sense when unpacking is enabled
	connect( unpackChk_, &QCheckBox::toggled, this, [ this ]( bool enabled )
	{
		deleteAfterExtractChk_->setEnabled( enabled );
	});

	connect( logChk_, &QCheckBox::toggled, this, [ this ]( bool checked )
	{
		logView_->setVisible( checked );
	});
}

//----------------------------------------------------------------------------------------------------------------------
// data loading

void CacowardsTab::loadData()
{
	QByteArray json;
	QString sourceDesc;

	if (!dataFilePath_.isEmpty() && QFile::exists( dataFilePath_ ))
	{
		QFile file( dataFilePath_ );
		if (file.open( QIODevice::ReadOnly ))
		{
			json = file.readAll();
			sourceDesc = dataFilePath_;
		}
	}

	if (json.isEmpty())
	{
		QFile file( ":/cacowards.json" );
		if (file.open( QIODevice::ReadOnly ))
		{
			json = file.readAll();
			sourceDesc = "built-in data";
		}
	}

	if (json.isEmpty() || !loadDataFromJson( json, entries_ ))
	{
		entries_.clear();
		setStatus( "No Cacowards data available. Click \"Refresh\" to download the list." );
		logMessage( "No Cacowards data available. Click Refresh to download the list." );
		buildTree();
		return;
	}

	buildTree( /*restoreExpansion*/ true );
	setStatus( QString( "Loaded %1 Cacowards entries (%2)." ).arg( entries_.size() ).arg( sourceDesc ) );
	logMessage( QString( "Loaded %1 Cacowards entries from %2." ).arg( entries_.size() ).arg( sourceDesc ) );
}

bool CacowardsTab::loadDataFromJson( const QByteArray & json, QList< CacowardEntry > & out ) const
{
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson( json, &parseError );
	if (parseError.error != QJsonParseError::NoError || !doc.isArray())
		return false;

	out.clear();
	const QJsonArray arr = doc.array();
	for (const QJsonValue & value : arr)
	{
		const QJsonObject obj = value.toObject();

		CacowardEntry entry;
		entry.year     = obj[ "year" ].toInt();
		entry.category = obj[ "category" ].toString();
		entry.title    = obj[ "title" ].toString();
		entry.id       = obj[ "id" ].toInt();
		entry.dir      = obj[ "dir" ].toString();
		entry.filename = obj[ "filename" ].toString();

		// keep entries that carry no download path too, so imported years stay visible
		// even when the /idgames API couldn't be reached to resolve an id
		out.append( entry );
	}

	return !out.isEmpty();
}

void CacowardsTab::buildTree( bool restoreExpansion )
{
	tree_->clear();

	// the tree is always built fully collapsed; the saved expansion state is applied only on purpose (e.g. on startup)

	// make sure entries are ordered by year, then category, then title
	std::sort( entries_.begin(), entries_.end(), []( const CacowardEntry & a, const CacowardEntry & b )
	{
		if (a.year != b.year)
			return a.year < b.year;
		const int ra = categoryRank( a.category ), rb = categoryRank( b.category );
		if (ra != rb)
			return ra < rb;
		return a.title.toLower() < b.title.toLower();
	});

	int prevYear = -1;
	QString prevCategory;
	QTreeWidgetItem * yearItem = nullptr;
	QTreeWidgetItem * categoryItem = nullptr;

	for (int i = 0; i < entries_.size(); ++i)
	{
		const CacowardEntry & entry = entries_[ i ];

		if (entry.year != prevYear)
		{
			yearItem = new QTreeWidgetItem( tree_, QStringList{ QString::number( entry.year ) } );
			yearItem->setFlags( yearItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
			yearItem->setCheckState( 0, Qt::Unchecked );
			prevYear = entry.year;
			prevCategory.clear();
		}

		if (entry.category != prevCategory)
		{
			categoryItem = new QTreeWidgetItem( yearItem, QStringList{ entry.category } );
			categoryItem->setFlags( categoryItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
			categoryItem->setCheckState( 0, Qt::Unchecked );
			prevCategory = entry.category;
		}

		auto * leaf = new QTreeWidgetItem( categoryItem, QStringList{ entry.title } );
		leaf->setFlags( leaf->flags() | Qt::ItemIsUserCheckable );
		leaf->setCheckState( 0, Qt::Unchecked );
		leaf->setData( 0, Qt::UserRole, i );  // index into entries_
	}

	// everything is collapsed by default; optionally re-apply the expansion state saved in a previous run
	if (restoreExpansion)
	{
		restoringExpansion_ = true;  // don't emit expansionChanged while programmatically restoring
		applyExpansionState();
		restoringExpansion_ = false;
	}
}

void CacowardsTab::applyExpansionState()
{
	// re-expand the nodes that were expanded when the app was closed, ignoring entries that no longer exist
	for (int i = 0; i < tree_->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem * yearItem = tree_->topLevelItem( i );
		const QString yearPath = yearItem->text( 0 );
		if (expandedNodes_.contains( yearPath ))
			yearItem->setExpanded( true );

		for (int c = 0; c < yearItem->childCount(); ++c)
		{
			QTreeWidgetItem * catItem = yearItem->child( c );
			const QString catPath = yearPath + '/' + catItem->text( 0 );
			if (expandedNodes_.contains( catPath ))
				catItem->setExpanded( true );
		}
	}
}

void CacowardsTab::onExpansionChanged()
{
	if (restoringExpansion_)
		return;  // this was a programmatic change while restoring, not a user action

	emit expansionChanged();
}

//----------------------------------------------------------------------------------------------------------------------
// list refresh

void CacowardsTab::refresh()
{
	if (refreshReply_)
		refreshReply_->abort();

	refreshEntries_.clear();
	refreshYears_.clear();
	for (int year = 2004; year <= QDate::currentDate().year(); ++year)
		refreshYears_.append( year );
	refreshYearIdx_ = 0;
	refreshResolveIdx_ = 0;
	refreshResolvedCount_ = 0;

	refreshBtn_->setEnabled( false );
	importBtn_->setEnabled( false );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, 0 );
		setStatus( "Refreshing Cacowards list..." );
		logMessage( QString( "Refreshing Cacowards list from doomwiki (years %1-%2)..." ).arg( 2004 ).arg( QDate::currentDate().year() ) );

	startNextRefreshYear();
}

void CacowardsTab::startNextRefreshYear()
{
	if (refreshYearIdx_ >= refreshYears_.size())
	{
		// all years fetched, now resolve the idgames ids into download paths
		if (refreshEntries_.isEmpty())
		{
			openBrowserExportPage();
			setStatus( "Automatic refresh was blocked. A doomwiki export page has been opened in your browser - click \"Export\" there to download the XML file, then press \"Import...\" here to load it." );
			return;
		}
		refreshResolveIdx_ = 0;
		refreshNetworkSteps_ = 0;
		refreshTimer_.start();
		setStatus( QString( "Resolving %1 entries..." ).arg( refreshEntries_.size() ) );
		logMessage( QString( "All years fetched; resolving download paths for %1 entries..." ).arg( refreshEntries_.size() ) );
		startNextRefreshResolution();
		return;
	}

	const int year = refreshYears_[ refreshYearIdx_ ];

	const QString urlString = QString::fromLatin1( kWaybackYearPrefix ) + QString::number( year );
	QUrl url( urlString );

	QNetworkRequest request( url );
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	refreshReply_ = network_->get( request );
	connect( refreshReply_, &QNetworkReply::finished, this, &CacowardsTab::onRefreshYearFetched );
}

void CacowardsTab::onRefreshYearFetched()
{
	QNetworkReply * reply = refreshReply_;
	refreshReply_ = nullptr;

	const int year = refreshYears_.value( refreshYearIdx_, 0 );

	if (reply)
	{
		const bool ok = reply->error() == QNetworkReply::NoError;
		const QString errorString = reply->errorString();
		const QByteArray data = reply->readAll();
		reply->deleteLater();

		if (ok)
		{
			const QString html = QString::fromUtf8( data );
			const int before = refreshEntries_.size();
			parseCacowardsHtml( year, html, refreshEntries_ );
			logMessage( QString( "Fetched Cacowards %1 (%2 entries)." ).arg( year ).arg( refreshEntries_.size() - before ) );
		}
		else
		{
			logMessage( QString( "Failed to fetch Cacowards %1: %2" ).arg( year ).arg( errorString ) );
		}
		// a failed year (404 for a year that doesn't exist, or a blocked request) is simply skipped
	}

	refreshYearIdx_++;
	startNextRefreshYear();
}

void CacowardsTab::parseCacowardsWikitext( int year, const QString & wikitext, QList< CacowardEntry > & out ) const
{
	static const QRegularExpression headingRe( QStringLiteral("^\\s*={2,6}\\s*(.+?)\\s*={2,6}\\s*$") );
	static const QRegularExpression idRe( QStringLiteral("\\{\\{ig\\|id=(\\d+)") );
	static const QRegularExpression fileRe( QStringLiteral("\\{\\{ig\\|file=([^}|]+)") );
	static const QRegularExpression titleRe( QStringLiteral("^\\s*\\*\\s*\\[\\[([^\\]|]+)") );

	QString currentCategory;

	const QStringList lines = wikitext.split( '\n' );
	for (const QString & line : lines)
	{
		const auto headingMatch = headingRe.match( line );
		if (headingMatch.hasMatch())
		{
			// keep the current category when a heading isn't an award section (e.g. a sub-heading)
			const QString category = classifyCategory( headingMatch.captured( 1 ).trimmed() );
			if (!category.isEmpty())
				currentCategory = category;
			continue;
		}

		const QString category = currentCategory;
		if (category.isEmpty())
			continue;

		// only list entries that carry an idgames link (either a numeric id or a file path)
		const auto idMatch = idRe.match( line );
		const auto fileMatch = fileRe.match( line );
		if (!idMatch.hasMatch() && !fileMatch.hasMatch())
			continue;

		QString title;
		const auto titleMatch = titleRe.match( line );
		if (titleMatch.hasMatch())
			title = titleMatch.captured( 1 ).trimmed();
		if (title.isEmpty())
			title = QStringLiteral("(unnamed)");

		CacowardEntry entry;
		entry.year = year;
		entry.category = category;
		entry.title = title;

		if (idMatch.hasMatch())
		{
			entry.id = idMatch.captured( 1 ).toInt();
		}
		else
		{
			// the file= link carries the path within the archive, e.g. "levels/doom2/Ports/megawads/valiant"
			const QString file = fileMatch.captured( 1 ).trimmed();
			const int slash = file.lastIndexOf( '/' );
			entry.dir = file.left( slash + 1 );
			entry.filename = file.mid( slash + 1 );
			if (!entry.filename.contains( '.' ))
				entry.filename += QLatin1String(".zip");
		}

		out.append( entry );
	}
}

void CacowardsTab::parseCacowardsHtml( int year, const QString & html, QList< CacowardEntry > & out ) const
{
	static const QRegularExpression headingRe( QStringLiteral("<h([2-4])[^>]*>(.*?)</h\\1>"),
	                                          QRegularExpression::DotMatchesEverythingOption );
	static const QRegularExpression liRe( QStringLiteral("<li[^>]*>(.*?)</li>"),
	                                     QRegularExpression::DotMatchesEverythingOption );
	static const QRegularExpression idRe( QStringLiteral("idgames[^\"']*[?&]id=(\\d+)") );
	static const QRegularExpression titleAttrRe( QStringLiteral("<a[^>]*href=\"[^\"]*doomwiki\\.org/wiki/[^\"]*\"[^>]*title=\"([^\"]*)\"") );
	static const QRegularExpression titleTextRe( QStringLiteral("<a[^>]*href=\"[^\"]*doomwiki\\.org/wiki/[^\"]*\"[^>]*>(.*?)</a>"),
	                                            QRegularExpression::DotMatchesEverythingOption );
	static const QRegularExpression tagRe( QStringLiteral("<[^>]+>") );

	// collect award headings (in document order) so each list item can be assigned a category
	struct Heading { int pos; QString category; };
	QList< Heading > headings;
	for (auto it = headingRe.globalMatch( html ); it.hasNext(); )
	{
		const auto m = it.next();
		QString text = m.captured( 2 );
		text.remove( tagRe );
		text = text.trimmed();
		const QString category = classifyCategory( text );
		if (!category.isEmpty())
			headings.append( Heading{ int( m.capturedStart() ), category } );
	}

	for (auto it = liRe.globalMatch( html ); it.hasNext(); )
	{
		const auto m = it.next();
		const int pos = int( m.capturedStart() );
		const QString content = m.captured( 1 );

		const auto idMatch = idRe.match( content );
		if (!idMatch.hasMatch())
			continue;

		QString title;
		const auto attrMatch = titleAttrRe.match( content );
		if (attrMatch.hasMatch())
		{
			title = attrMatch.captured( 1 ).trimmed();
		}
		else
		{
			const auto textMatch = titleTextRe.match( content );
			if (textMatch.hasMatch())
			{
				title = textMatch.captured( 1 );
				title.remove( tagRe );
				title = title.trimmed();
			}
		}
		if (title.isEmpty())
			title = QStringLiteral("(unnamed)");

		// the category is the nearest award heading preceding this item
		QString category;
		for (const Heading & h : headings)
		{
			if (h.pos < pos)
				category = h.category;
			else
				break;
		}
		if (category.isEmpty())
			continue;

		CacowardEntry entry;
		entry.year = year;
		entry.category = category;
		entry.title = title;
		entry.id = idMatch.captured( 1 ).toInt();
		out.append( entry );
	}
}

void CacowardsTab::parseExportXml( const QByteArray & xml, QList< CacowardEntry > & out ) const
{
	QXmlStreamReader reader( xml );

	QString pageTitle;
	QString pageText;

	while (!reader.atEnd())
	{
		switch (reader.readNext())
		{
			case QXmlStreamReader::StartElement:
				if (reader.name().toString() == QLatin1String("title"))
					pageTitle = reader.readElementText();
				else if (reader.name().toString() == QLatin1String("text"))
					pageText = reader.readElementText();
				break;

			case QXmlStreamReader::EndElement:
				if (reader.name().toString() == QLatin1String("page"))
				{
					if (pageTitle.startsWith( QLatin1String("Cacowards_") ) || pageTitle.startsWith( QLatin1String("Cacowards ") ))
					{
						bool ok = false;
						const int year = pageTitle.mid( 10 ).trimmed().toInt( &ok );  // len("Cacowards_") == len("Cacowards ") == 10
						if (ok)
							parseCacowardsWikitext( year, pageText, out );
					}
					pageTitle.clear();
					pageText.clear();
				}
				break;

			default:
				break;
		}
	}
}

void CacowardsTab::openBrowserExportPage()
{
	progressBar_->setVisible( false );
	refreshBtn_->setEnabled( true );
	importBtn_->setEnabled( true );

	// open doomwiki's Special:Export page in the user's browser with all Cacowards pages pre-listed
	QStringList pageNames;
	for (int year = 2004; year <= QDate::currentDate().year(); ++year)
		pageNames.append( QString( "Cacowards_%1" ).arg( year ) );

	QUrl url( kDoomwikiIndex );
	QUrlQuery query;
	query.addQueryItem( "title", "Special:Export" );
	query.addQueryItem( "pages", pageNames.join( '\n' ) );
	url.setQuery( query );

	QDesktopServices::openUrl( url );
}

void CacowardsTab::generateList()
{
	openBrowserExportPage();
	setStatus( "Opened doomwiki's export page in your browser - click \"Export\" there to download the XML file, then press \"Import...\" here to load it." );
	logMessage( "Opened doomwiki's export page in your browser." );
}

void CacowardsTab::importExportedXml()
{
	const QString filePath = QFileDialog::getOpenFileName( this, "Import doomwiki export XML", QString(), "XML files (*.xml);;All files (*)" );
	if (filePath.isEmpty())
		return;

	QFile file( filePath );
	if (!file.open( QIODevice::ReadOnly ))
	{
		setStatus( "Could not open \"" + filePath + "\"." );
		logMessage( "Import failed: could not open \"" + filePath + "\"." );
		return;
	}

	QList< CacowardEntry > entries;
	parseExportXml( file.readAll(), entries );

	if (entries.isEmpty())
	{
		setStatus( "No Cacowards entries found in \"" + filePath + "\"." );
		logMessage( "Import failed: no Cacowards entries found in \"" + filePath + "\"." );
		return;
	}

	logMessage( QString( "Imported %1 entries from \"%2\"." ).arg( entries.size() ).arg( filePath ) );

	// Don't contact the /idgames API to resolve/download paths while importing: take the
	// paths the XML carries directly (file= references). Entries referenced only by an
	// id= are imported as-is (listed, but with no path until the API is reachable).
	refreshEntries_ = entries;
	entries_ = entries;
	saveDataFile();
	buildTree();
	emit expansionChanged();

	refreshBtn_->setEnabled( true );
	importBtn_->setEnabled( true );
	progressBar_->setVisible( false );

	int withPath = 0;
	for (const CacowardEntry & entry : entries)
		if (!entry.dir.isEmpty() && !entry.filename.isEmpty())
			++withPath;
	const int idOnly = entries.size() - withPath;

	setStatus( QString( "Imported %1 Cacowards entries (%2 with a download path, %3 by /idgames id). The list is cached, so you don't need to import again." ).arg( entries.size() ).arg( withPath ).arg( idOnly ) );
	if (idOnly > 0)
		logMessage( QString( "%1 entries carry only an /idgames id and were left without a path (no network check was performed)." ).arg( idOnly ) );
}

void CacowardsTab::startNextRefreshResolution()
{
	// entries that already carry a download path (from a file= idgames link) need no API lookup
	while (refreshResolveIdx_ < refreshEntries_.size()
		&& !refreshEntries_[ refreshResolveIdx_ ].dir.isEmpty()
		&& !refreshEntries_[ refreshResolveIdx_ ].filename.isEmpty())
	{
		refreshResolveIdx_++;
	}

	// all entries processed - keep every entry (even ones that couldn't be resolved),
	// so that all years/entries remain visible; unresolved ones are just not downloadable
	if (refreshResolveIdx_ >= refreshEntries_.size())
	{
		int unresolved = 0;
		for (const CacowardEntry & entry : refreshEntries_)
			if (entry.dir.isEmpty() || entry.filename.isEmpty())
				++unresolved;

		// persist the list so that the user doesn't have to import the XML / refresh again next time
		entries_ = refreshEntries_;
		saveDataFile();
		buildTree();

		// the rebuilt tree is fully collapsed; let the launcher remember this expansion state
		emit expansionChanged();

		progressBar_->setVisible( false );
		refreshBtn_->setEnabled( true );
		importBtn_->setEnabled( true );
		setStatus( QString( "Loaded and saved %1 Cacowards entries. The list is cached, so you don't need to import the XML again." ).arg( entries_.size() ) );
		if (unresolved > 0)
			logMessage( QString( "Done: %1 entries kept, of which %2 could not be resolved to a download path (the /idgames API may have been unreachable); they are listed but not downloadable." ).arg( entries_.size() ).arg( unresolved ) );
		else
			logMessage( QString( "Done: resolved and saved %1 Cacowards entries." ).arg( entries_.size() ) );
		return;
	}

	const CacowardEntry & entry = refreshEntries_[ refreshResolveIdx_ ];

	// show which entry is being reviewed and how much work is left
	updateResolveProgress( entry );

	QUrl url( kIdgamesApiBase );
	QUrlQuery query;
	query.addQueryItem( "action", "get" );
	query.addQueryItem( "id", QString::number( entry.id ) );
	query.addQueryItem( "out", "json" );
	url.setQuery( query );

	QNetworkRequest request( url );
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	refreshReply_ = network_->get( request );
	connect( refreshReply_, &QNetworkReply::finished, this, &CacowardsTab::onRefreshIdResolved );
}

void CacowardsTab::onRefreshIdResolved()
{
	QNetworkReply * reply = refreshReply_;
	refreshReply_ = nullptr;

	if (reply && reply->error() == QNetworkReply::NoError)
	{
		const QByteArray data = reply->readAll();

		const QJsonDocument doc = QJsonDocument::fromJson( data );
		if (doc.isObject())
		{
			const QJsonObject content = doc.object()[ "content" ].toObject();
			const QString dir = content[ "dir" ].toString();
			const QString filename = content[ "filename" ].toString();
			if (!dir.isEmpty() && !filename.isEmpty() && refreshResolveIdx_ < refreshEntries_.size())
			{
				refreshEntries_[ refreshResolveIdx_ ].dir = dir;
				refreshEntries_[ refreshResolveIdx_ ].filename = filename;
				refreshResolvedCount_++;
			}
			else if (refreshResolveIdx_ < refreshEntries_.size())
			{
				logMessage( QString( "Could not resolve \"%1\" (%2) - no download path returned." )
					.arg( refreshEntries_[ refreshResolveIdx_ ].title ).arg( refreshEntries_[ refreshResolveIdx_ ].id ) );
			}
		}
	}
	else if (refreshResolveIdx_ < refreshEntries_.size())
	{
		logMessage( QString( "Could not resolve \"%1\" (%2): %3" )
			.arg( refreshEntries_[ refreshResolveIdx_ ].title )
			.arg( refreshEntries_[ refreshResolveIdx_ ].id )
			.arg( reply ? reply->errorString() : QStringLiteral("no reply") ) );
	}
	if (reply)
		reply->deleteLater();

	refreshNetworkSteps_++;
	refreshResolveIdx_++;
	startNextRefreshResolution();
}

void CacowardsTab::updateResolveProgress( const CacowardEntry & entry )
{
	const int total = refreshEntries_.size();
	const int done = refreshResolveIdx_;                    // entries fully processed so far
	const int current = refreshResolveIdx_ + 1;             // 1-based index of the entry being worked on

	progressBar_->setVisible( true );
	progressBar_->setRange( 0, total );
	progressBar_->setValue( qBound( 0, done, total ) );

	// count the entries that still need an idgames lookup (those already carrying a path are skipped for free)
	int remaining = 0;
	for (int i = refreshResolveIdx_; i < total; ++i)
		if (refreshEntries_[ i ].dir.isEmpty() || refreshEntries_[ i ].filename.isEmpty())
			++remaining;

	const int percent = total > 0 ? int( done * 100.0 / total ) : 0;
	const QString title = entry.title.isEmpty() ? entry.filename : entry.title;

	QString status = QString( "Reviewing %1 of %2: %3 (%4%)" )
		.arg( current ).arg( total ).arg( title ).arg( percent );

	// estimate how long the rest will take based on the average time each API lookup took so far
	if (refreshNetworkSteps_ > 0 && refreshTimer_.isValid())
	{
		const double msPerStep = double( refreshTimer_.elapsed() ) / refreshNetworkSteps_;
		const qint64 etaMs = qint64( msPerStep * remaining + 0.5 );
		status += QString( "  (~%1 left)" ).arg( formatRemainingEstimate( etaMs ) );
	}

	setStatus( status );
}

QString CacowardsTab::formatRemainingEstimate( qint64 ms )
{
	const qint64 seconds = qMax< qint64 >( 0, ms / 1000 );
	if (seconds < 60)
		return QStringLiteral( "%1s" ).arg( seconds );

	const qint64 minutes = seconds / 60;
	if (minutes < 60)
		return QStringLiteral( "%1m %2s" ).arg( minutes ).arg( seconds % 60 );

	return QStringLiteral( "%1h %2m" ).arg( minutes / 60 ).arg( minutes % 60 );
}

void CacowardsTab::saveDataFile()
{
	if (dataFilePath_.isEmpty())
		return;

	QJsonArray arr;
	for (const CacowardEntry & entry : refreshEntries_)
	{
		QJsonObject obj;
		obj[ "year" ] = entry.year;
		obj[ "category" ] = entry.category;
		obj[ "title" ] = entry.title;
		obj[ "id" ] = entry.id;
		obj[ "dir" ] = entry.dir;
		obj[ "filename" ] = entry.filename;
		arr.append( obj );
	}

	QFile file( dataFilePath_ );
	if (file.open( QIODevice::WriteOnly ))
	{
		file.write( QJsonDocument( arr ).toJson( QJsonDocument::Indented ) );
		file.close();
		logMessage( "Saved Cacowards list to \"" + dataFilePath_ + "\"." );
	}
}

//----------------------------------------------------------------------------------------------------------------------
// tree interaction

void CacowardsTab::onCurrentItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * /*previous*/ )
{
	if (!current || current->childCount() != 0)
	{
		detailsView_->clear();
		return;
	}

	const int idx = current->data( 0, Qt::UserRole ).toInt();
	if (idx < 0 || idx >= entries_.size())
		return;

	const CacowardEntry & entry = entries_[ idx ];

	QString text;
	text += "Title: " + entry.title + '\n';
	text += "Year: " + QString::number( entry.year ) + '\n';
	text += "Category: " + entry.category + '\n';
	text += "File: " + entry.filename + '\n';

	detailsView_->setPlainText( text );
}

void CacowardsTab::onItemChanged( QTreeWidgetItem * /*item*/, int column )
{
	// the year/category/leaf check states are kept in sync by Qt's ItemIsAutoTristate flag,
	// so here we only need to reflect the current state on the download button
	if (column != 0)
		return;

	updateDownloadBtnState();
}

//----------------------------------------------------------------------------------------------------------------------
// downloading

void CacowardsTab::browseTargetDir()
{
	const QString dir = QFileDialog::getExistingDirectory( this, "Select download directory", targetDir() );
	if (dir.isEmpty())  // user cancelled
		return;

	targetDirLine_->setText( dir );  // the textChanged connection emits targetDirChanged
}

QList< CacowardsTab::PendingDownload > CacowardsTab::collectCheckedDownloads() const
{
	QList< PendingDownload > downloads;

	QTreeWidgetItemIterator it( tree_, QTreeWidgetItemIterator::Checked );
	while (*it)
	{
		QTreeWidgetItem * item = *it;
		++it;

		if (item->childCount() != 0)  // only leaf items represent downloadable files
			continue;

		const int idx = item->data( 0, Qt::UserRole ).toInt();
		if (idx < 0 || idx >= entries_.size())
			continue;

		const CacowardEntry & entry = entries_[ idx ];

		// Entries carrying only an /idgames id (not resolved at import time) have no path
		// yet; they'll be resolved lazily right before the download starts.
		if (entry.filename.isEmpty())
		{
			if (entry.id != 0)
				downloads.append( PendingDownload{ QString(), QString(), entry.title, entry.id, entry.year, entry.category } );
			continue;
		}

		const QString fileName = sanitizeFileName( entry.filename );

		QString subDir;
		if (autosortChk_->isChecked())
			subDir = QString( "Cacowards/%1/%2" ).arg( entry.year ).arg( sanitizeFileName( entry.category ) );

		const QString filePath = QDir( targetDir() ).filePath( subDir.isEmpty() ? fileName : subDir + '/' + fileName );

		downloads.append( PendingDownload{ filePath, entry.relativePath(), entry.title, 0, entry.year, entry.category } );
	}

	return downloads;
}

void CacowardsTab::updateDownloadBtnState()
{
	QTreeWidgetItemIterator it( tree_, QTreeWidgetItemIterator::Checked );
	bool anyChecked = false;
	while (*it)
	{
		QTreeWidgetItem * item = *it;
		++it;
		if (item->childCount() == 0)
		{
			anyChecked = true;
			break;
		}
	}

	downloadBtn_->setEnabled( !batchActive_ && anyChecked );
}

void CacowardsTab::downloadChecked()
{
	if (!ensureTargetDirUsable())
		return;  // a message has been shown, and possibly the target dir was switched to the map dir

	QList< PendingDownload > allDownloads = collectCheckedDownloads();
	if (allDownloads.isEmpty())
	{
		setStatus( "Check the entries you want to download." );
		return;
	}

	QStringList existingNames;
	for (const PendingDownload & d : allDownloads)
	{
		if (QFile::exists( d.filePath ))
			existingNames.append( QFileInfo( d.filePath ).fileName() );
	}

	if (!existingNames.isEmpty())
	{
		const auto answer = QMessageBox::question(
			this, "Files already exist",
			QString( "%1 of the selected file(s) already exist in the target directory.\nOverwrite them?" ).arg( existingNames.size() ),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No
		);
		if (answer != QMessageBox::Yes)
		{
			pendingDownloads_.clear();
			for (const PendingDownload & d : allDownloads)
				if (!QFile::exists( d.filePath ))
					pendingDownloads_.append( d );
			logMessage( QString( "%1 file(s) already exist and will be skipped." ).arg( existingNames.size() ) );
		}
		else
		{
			pendingDownloads_ = allDownloads;
			logMessage( QString( "%1 file(s) already exist and will be overwritten." ).arg( existingNames.size() ) );
		}
	}
	else
	{
		pendingDownloads_ = allDownloads;
	}

	if (pendingDownloads_.isEmpty())
	{
		setStatus( "Nothing to download (all selected files already exist)." );
		logMessage( "Nothing to download - all selected files already exist." );
		return;
	}

	logMessage( QString( "Starting download of %1 file(s) to \"%2\"%3." )
		.arg( pendingDownloads_.size() )
		.arg( targetDir() )
		.arg( autosortChk_->isChecked() ? " (autosort)" : "" ) );

	batchActive_ = true;
	downloadCount_ = pendingDownloads_.size();
	downloadBtn_->setEnabled( false );

	startNextDownload();
}

void CacowardsTab::startNextDownload()
{
	// fill all free download slots up to the allowed parallelism
	while (activeDownloads_.size() < size_t( maxParallel_ ) && !pendingDownloads_.isEmpty())
	{
		const PendingDownload next = pendingDownloads_.takeFirst();

		auto download = std::make_unique< ActiveDownload >();
		download->path = next.filePath;
		download->title = next.title;
		download->resolveId = next.id;
		download->year = next.year;
		download->category = next.category;
		if (next.id == 0)  // the path is already known, build the mirror URLs right away
		{
			for (const QString & base : shuffledMirrors())
				download->urls.append( base + next.relativePath );
		}

		ActiveDownload * raw = download.get();
		activeDownloads_.push_back( std::move( download ) );
		startDownload( raw );
	}

	updateDownloadProgress();

	if (activeDownloads_.empty())
	{
		batchActive_ = false;
		downloadCount_ = 0;
		progressBar_->setVisible( false );
		setStatus( "All downloads finished." );
		logMessage( "All downloads finished." );
		updateDownloadBtnState();
		return;
	}
}

void CacowardsTab::startDownload( ActiveDownload * download )
{
	// This entry carries only an /idgames id - resolve it to a path first, then download.
	if (download->resolveId != 0)
	{
		QUrl url( kIdgamesApiBase );
		QUrlQuery query;
		query.addQueryItem( "action", "get" );
		query.addQueryItem( "id", QString::number( download->resolveId ) );
		query.addQueryItem( "out", "json" );
		url.setQuery( query );

		QNetworkRequest request( url );
		request.setRawHeader( "User-Agent", kUserAgent );
		request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

		download->resolveReply = network_->get( request );
		connect( download->resolveReply, &QNetworkReply::finished, this, [ this, download ]()
		{
			onDownloadIdResolved( download );
		});
		return;
	}

	if (download->urlIdx >= download->urls.size())
		return;

	const QString url = download->urls[ download->urlIdx ];

	QNetworkRequest request{ QUrl( url ) };
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	// make sure the parent directory exists (it may be a nested autosort subfolder)
	QDir().mkpath( QFileInfo( download->path ).absolutePath() );

	QFile * file = new QFile( download->path );
	if (!file->open( QIODevice::WriteOnly ))
	{
		setStatus( "Could not open \"" + download->path + "\" for writing." );
		logMessage( "Download failed: could not open \"" + download->path + "\" for writing." );
		delete file;
		// free this slot and let the batch continue
		removeDownload( download );
		startNextDownload();
		return;
	}
	download->file = file;

	download->reply = network_->get( request );

	// stream the incoming data directly to disk
	connect( download->reply, &QNetworkReply::readyRead, this, [ this, download ]()
	{
		if (download->file && download->reply)
			download->file->write( download->reply->readAll() );
	});

	connect( download->reply, &QNetworkReply::finished, this, [ this, download ]()
	{
		onDownloadFinished( download );
	});

	downloadBtn_->setEnabled( false );
	updateDownloadProgress();

	const int startedCnt = downloadCount_ - pendingDownloads_.size() - activeDownloads_.size() + 1;
	QString batchInfo = downloadCount_ > 0 ? QString( " [%1/%2]" ).arg( startedCnt ).arg( downloadCount_ ) : QString();
	setStatus( QString( "Downloading \"%1\"%2 ..." )
		.arg( QFileInfo( download->path ).fileName() )
		.arg( batchInfo ) );
	logMessage( QString( "Downloading \"%1\"%2 ..." )
		.arg( QFileInfo( download->path ).fileName() )
		.arg( batchInfo ) );
}

void CacowardsTab::onDownloadIdResolved( ActiveDownload * download )
{
	QNetworkReply * reply = download->resolveReply;
	download->resolveReply = nullptr;

	bool ok = false;
	QString dir, filename;
	if (reply && reply->error() == QNetworkReply::NoError)
	{
		const QJsonDocument doc = QJsonDocument::fromJson( reply->readAll() );
		if (doc.isObject())
		{
			const QJsonObject content = doc.object()[ "content" ].toObject();
			dir = content[ "dir" ].toString();
			filename = content[ "filename" ].toString();
			ok = !dir.isEmpty() && !filename.isEmpty();
		}
	}
	if (reply)
		reply->deleteLater();

	if (ok)
	{
		// rebuild the save path and the mirror URLs from the resolved path, then download
		const QString fileName = sanitizeFileName( filename );
		QString subDir;
		if (autosortChk_->isChecked())
			subDir = QString( "Cacowards/%1/%2" ).arg( download->year ).arg( sanitizeFileName( download->category ) );
		download->path = QDir( targetDir() ).filePath( subDir.isEmpty() ? fileName : subDir + '/' + fileName );

		const QString relativePath = dir + fileName;  // dir always ends with '/'
		download->urls.clear();
		for (const QString & base : shuffledMirrors())
			download->urls.append( base + relativePath );

		download->resolveId = 0;
		logMessage( QString( "Resolved \"%1\" to %2." ).arg( download->title ).arg( relativePath ) );
		startDownload( download );
	}
	else
	{
		logMessage( QString( "Could not resolve \"%1\" (id=%2): %3" )
			.arg( download->title )
			.arg( download->resolveId )
			.arg( reply ? reply->errorString() : QStringLiteral("no reply") ) );
		removeDownload( download );
		startNextDownload();
	}
}

void CacowardsTab::onDownloadFinished( ActiveDownload * download )
{
	QNetworkReply * reply = download->reply;
	download->reply = nullptr;

	// flush whatever remains in the reply's buffer
	if (download->file)
	{
		if (reply)
			download->file->write( reply->readAll() );
		download->file->close();
	}

	QString errorString;
	bool success = true;
	if (reply)
	{
		success = reply->error() == QNetworkReply::NoError;
		errorString = reply->errorString();
		reply->deleteLater();
	}

	const QString savedPath = download->path;
	const QString savedTitle = download->title;

	if (success)
	{
		if (download->file)
		{
			download->file->deleteLater();
			download->file = nullptr;
		}

		// sanity check: a successful download must not produce an empty file
		if (QFileInfo( savedPath ).size() <= 0)
		{
			success = false;
			errorString = "downloaded file is empty";
		}
	}

	if (success)
	{
		logMessage( QString( "Downloaded \"%1\" (%2 bytes)." ).arg( savedPath ).arg( QFileInfo( savedPath ).size() ) );

		if (unpackChk_->isChecked())
		{
			// Unpack in a background worker thread so that the UI stays responsive
			// and multiple downloads/unpacks can run at the same time.
			const QFileInfo savedInfo( savedPath );
			const QString title = savedTitle.trimmed();
			const QString folderName = title.isEmpty() ? savedInfo.completeBaseName() : sanitizeFileName( title );
			download->extractDir = savedInfo.dir().filePath( folderName );

			download->unpackWatcher = new QFutureWatcher< bool >( this );
			connect( download->unpackWatcher, &QFutureWatcher< bool >::finished, this, [ this, download ]()
			{
				onUnpackFinished( download );
			});

			const QString zipPath = savedPath;
			const QString extractDir = download->extractDir;
			download->unpackWatcher->setFuture( QtConcurrent::run( [ zipPath, extractDir ]()
			{
				return extractZipArchive( zipPath, extractDir );
			}) );

			setStatus( "Unpacking \"" + extractDir + "\" ..." );
			logMessage( "Unpacking \"" + zipPath + "\" into \"" + extractDir + "\" ..." );
			downloadBtn_->setEnabled( false );
			updateDownloadProgress();
			return;  // the download slot stays occupied until the unpacking finishes
		}
		else
		{
			setStatus( "Downloaded to \"" + savedPath + "\"." );
			emit downloadFinished( savedPath );
		}
	}
	else
	{
		if (errorString.isEmpty())
			errorString = "download failed";

		// remove the partially-written / empty file
		if (!savedPath.isEmpty())
			QFile::remove( savedPath );

		if (download->file)
		{
			download->file->deleteLater();
			download->file = nullptr;
		}

		// try the next mirror, if there is one
		++download->urlIdx;
		if (download->urlIdx < download->urls.size())
		{
			setStatus( "Mirror failed (" + errorString + "), trying the next one ..." );
			logMessage( QString( "Mirror failed for \"%1\" (%2); trying the next mirror." ).arg( QFileInfo( savedPath ).fileName() ).arg( errorString ) );
			startDownload( download );
			return;
		}

		setStatus( "Download failed: " + errorString );
		logMessage( QString( "Failed to download \"%1\": %2" ).arg( QFileInfo( savedPath ).fileName() ).arg( errorString ) );
	}

	updateDownloadProgress();
	removeDownload( download );
	startNextDownload();
}

void CacowardsTab::onUnpackFinished( ActiveDownload * download )
{
	QFutureWatcher< bool > * watcher = download->unpackWatcher;
	download->unpackWatcher = nullptr;
	const bool unpackSucceeded = watcher->result();
	watcher->deleteLater();

	const QString savedPath = download->path;
	const QString extractDir = download->extractDir;

	if (unpackSucceeded)
	{
		if (deleteAfterExtractChk_->isChecked())
		{
			QFile::remove( savedPath );
			logMessage( "Deleted the archive \"" + savedPath + "\"." );
		}

		setStatus( "Unpacked to \"" + extractDir + "\"." );
		logMessage( "Unpacked \"" + savedPath + "\" into \"" + extractDir + "\" successfully." );
		emit downloadFinished( extractDir );
	}
	else
	{
		setStatus( "Downloaded to \"" + savedPath + "\", but unpacking failed." );
		logMessage( "Unpacking failed for \"" + savedPath + "\" (the archive was downloaded but not extracted)." );
		emit downloadFinished( savedPath );
	}

	updateDownloadProgress();
	removeDownload( download );
	startNextDownload();
}

//----------------------------------------------------------------------------------------------------------------------

QString CacowardsTab::sanitizeFileName( const QString & fileName ) const
{
	QString name = fileName;

	// strip any path components, keep only the file name
	name = name.section( '/', -1 ).section( '\\', -1 );

	// remove characters that are invalid or problematic on common file systems
	name.remove( QRegularExpression( "[<>:\"/\\\\|?*\\x00-\\x1f]" ) );

	if (name.isEmpty())
		name = "download";

	return name;
}

bool CacowardsTab::ensureTargetDirUsable()
{
	QString dir = targetDir();

	// Offer the community map directory as an alternative whenever the current target
	// is empty, does not exist, or is not writable.
	if (dir.isEmpty() || !QFileInfo::exists( dir ) || !fs::isDirectoryWritable( dir ))
	{
		if (askToUseMapDir( dir ))
			return true;  // targetDirLine_ now points to the map directory

		dir = targetDir();  // the user declined the suggestion, re-read the (unchanged) target
	}

	if (dir.isEmpty())
	{
		setStatus( "Choose a target directory first." );
		return false;
	}

	// create it if it doesn't exist yet
	if (!QFileInfo::exists( dir ))
	{
		if (!QDir().mkpath( dir ))
		{
			QMessageBox::warning( this, "Target directory not usable",
				"Target directory \"" + dir + "\" is not accessible for writing.\nPlease choose another directory." );
			logMessage( "Target directory \"" + dir + "\" could not be created." );
			return false;
		}
		logMessage( "Created target directory \"" + dir + "\"." );
	}

	// it must also be writable for the downloaded files to be saved into it
	if (!fs::isDirectoryWritable( dir ))
	{
		QMessageBox::warning( this, "Target directory not usable",
			"Target directory \"" + dir + "\" is not accessible for writing.\nPlease choose another directory." );
		logMessage( "Target directory \"" + dir + "\" is not writable." );
		return false;
	}

	return true;
}

bool CacowardsTab::askToUseMapDir( const QString & targetDir )
{
	// Only ever suggest the map directory if it actually exists and is writable.
	if (!mapSourceDir_.isEmpty() && QFileInfo::exists( mapSourceDir_ ) && fs::isDirectoryWritable( mapSourceDir_ ))
	{
		const QString targetDesc = targetDir.isEmpty()
			? "The target directory"
			: "Target directory \"" + targetDir + "\"";
		const auto answer = QMessageBox::question(
			this, "Target directory not usable",
			targetDesc + " is empty, does not exist, or is not writable.\n\n"
			"Use your map directory \"" + mapSourceDir_ + "\" as the download target instead?",
			QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes
		);
		if (answer == QMessageBox::Yes)
		{
			targetDirLine_->setText( mapSourceDir_ );  // also re-saves the target dir preference
			logMessage( "Switched the download target to the map directory \"" + mapSourceDir_ + "\"." );
			return true;
		}
	}

	return false;
}

void CacowardsTab::removeDownload( ActiveDownload * download )
{
	for (size_t i = 0; i < activeDownloads_.size(); ++i)
	{
		if (activeDownloads_[ i ].get() == download)
		{
			activeDownloads_.erase( activeDownloads_.begin() + qsizetype( i ) );
			return;
		}
	}
}

void CacowardsTab::updateDownloadProgress()
{
	if (downloadCount_ <= 0)
		return;

	const int completed = downloadCount_ - pendingDownloads_.size() - int( activeDownloads_.size() );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, downloadCount_ );
	progressBar_->setValue( qBound( 0, completed, downloadCount_ ) );
}

void CacowardsTab::setStatus( const QString & text )
{
	statusLabel_->setText( text );
}

void CacowardsTab::logMessage( const QString & message )
{
	logView_->appendPlainText( QString( "[%1] %2" ).arg( QTime::currentTime().toString( "HH:mm:ss" ) ).arg( message ) );

	// keep the log scrolled to the newest entry
	if (QScrollBar * bar = logView_->verticalScrollBar())
		bar->setValue( bar->maximum() );
}
