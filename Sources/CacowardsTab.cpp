//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      (cacowards browser feature)
// Description: tab showing an expandable, checkable list of Cacowards-awarded WADs, downloadable from /idgames
//======================================================================================================================

#include "CacowardsTab.hpp"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
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
#include <QXmlStreamReader>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>


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
	"https://ftp.gamers.org/pub/idgames/",
};

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

/// Recursively applies a check state to an item and all of its descendants.
void setCheckStateRecursively( QTreeWidgetItem * item, Qt::CheckState state )
{
	for (int i = 0; i < item->childCount(); ++i)
	{
		QTreeWidgetItem * child = item->child( i );
		child->setCheckState( 0, state );
		setCheckStateRecursively( child, state );
	}
}

/// Updates the check state of an item's ancestors to reflect the state of their children.
void updateAncestorCheckStates( QTreeWidgetItem * parent )
{
	while (parent)
	{
		int checkedCnt = 0, uncheckedCnt = 0;
		for (int i = 0; i < parent->childCount(); ++i)
		{
			const Qt::CheckState state = parent->child( i )->checkState( 0 );
			if (state == Qt::Checked)
				checkedCnt++;
			else if (state == Qt::Unchecked)
				uncheckedCnt++;
		}

		Qt::CheckState parentState;
		if (checkedCnt == 0)
			parentState = Qt::Unchecked;
		else if (uncheckedCnt == 0)
			parentState = Qt::Checked;
		else
			parentState = Qt::PartiallyChecked;

		parent->setCheckState( 0, parentState );
		parent = parent->parent();
	}
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
	if (downloadReply_)
		downloadReply_->abort();
	if (refreshReply_)
		refreshReply_->abort();
	delete downloadFile_;
}

void CacowardsTab::setTargetDir( const QString & dir )
{
	targetDirLine_->setText( dir );
}

QString CacowardsTab::targetDir() const
{
	return targetDirLine_->text().trimmed();
}

void CacowardsTab::setDataFilePath( const QString & path )
{
	dataFilePath_ = path;
	loadData();
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
	mainLayout->addWidget( progressBar_ );
	mainLayout->addWidget( statusLabel_ );

	//-- signal/slot wiring -----------------------------------------------------

	connect( refreshBtn_, &QPushButton::clicked, this, &CacowardsTab::refresh );
	connect( generateListBtn_, &QPushButton::clicked, this, &CacowardsTab::generateList );
	connect( importBtn_, &QPushButton::clicked, this, &CacowardsTab::importExportedXml );
	connect( tree_, &QTreeWidget::currentItemChanged, this, &CacowardsTab::onCurrentItemChanged );
	connect( tree_, &QTreeWidget::itemChanged, this, &CacowardsTab::onItemChanged );
	connect( browseBtn_, &QPushButton::clicked, this, &CacowardsTab::browseTargetDir );
	connect( downloadBtn_, &QPushButton::clicked, this, &CacowardsTab::downloadChecked );

	// save the target directory as soon as the user changes it
	connect( targetDirLine_, &QLineEdit::textChanged, this, [ this ]( const QString & text )
	{
		emit targetDirChanged( text.trimmed() );
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
		buildTree();
		return;
	}

	buildTree();
	setStatus( QString( "Loaded %1 Cacowards entries (%2)." ).arg( entries_.size() ).arg( sourceDesc ) );
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

		if (entry.dir.isEmpty() || entry.filename.isEmpty())
			continue;  // not downloadable

		out.append( entry );
	}

	return !out.isEmpty();
}

void CacowardsTab::buildTree()
{
	tree_->clear();

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

	tree_->expandToDepth( 1 );  // show years and categories, keep wads collapsed
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
		setStatus( QString( "Resolving %1 entries..." ).arg( refreshEntries_.size() ) );
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
		const QByteArray data = reply->readAll();
		reply->deleteLater();

		if (ok)
		{
			const QString html = QString::fromUtf8( data );
			parseCacowardsHtml( year, html, refreshEntries_ );
		}
		// a failed year (404 for a year that doesn't exist, or a blocked request) is simply skipped
	}

	refreshYearIdx_++;
	startNextRefreshYear();
}

void CacowardsTab::parseCacowardsWikitext( int year, const QString & wikitext, QList< CacowardEntry > & out ) const
{
	static const QRegularExpression headingRe( QStringLiteral("^\\s*={2,6}\\s*(.+?)\\s*={2,6}\\s*$") );
	static const QRegularExpression idRe( QStringLiteral("id=(\\d+)") );
	static const QRegularExpression titleRe( QStringLiteral("'''\\[\\[([^\\]|]+)") );

	QString currentCategory;

	const QStringList lines = wikitext.split( '\n' );
	for (const QString & line : lines)
	{
		const auto headingMatch = headingRe.match( line );
		if (headingMatch.hasMatch())
		{
			currentCategory = classifyCategory( headingMatch.captured( 1 ).trimmed() );
			continue;
		}

		// only list entries that carry a direct idgames link
		if (!line.contains( "idgames" ) || !line.contains( "id=" ))
			continue;

		const auto idMatch = idRe.match( line );
		if (!idMatch.hasMatch())
			continue;

		const QString category = currentCategory;  // (already classified)
		if (category.isEmpty())
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
		entry.id = idMatch.captured( 1 ).toInt();
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
					if (pageTitle.startsWith( QLatin1String("Cacowards_") ))
					{
						bool ok = false;
						const int year = pageTitle.mid( 10 ).toInt( &ok );  // len("Cacowards_") == 10
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
		return;
	}

	QList< CacowardEntry > entries;
	parseExportXml( file.readAll(), entries );

	if (entries.isEmpty())
	{
		setStatus( "No Cacowards entries found in \"" + filePath + "\"." );
		return;
	}

	refreshEntries_ = entries;
	refreshResolveIdx_ = 0;
	refreshResolvedCount_ = 0;

	refreshBtn_->setEnabled( false );
	importBtn_->setEnabled( false );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, 0 );
	setStatus( QString( "Resolving %1 entries..." ).arg( entries.size() ) );

	startNextRefreshResolution();
}

void CacowardsTab::startNextRefreshResolution()
{
	// skip entries that couldn't be resolved (they have no download path)
	if (refreshResolveIdx_ >= refreshEntries_.size())
	{
		QList< CacowardEntry > resolved;
		for (const CacowardEntry & entry : refreshEntries_)
			if (!entry.dir.isEmpty() && !entry.filename.isEmpty())
				resolved.append( entry );

		saveDataFile();
		entries_ = resolved;
		buildTree();

		progressBar_->setVisible( false );
		refreshBtn_->setEnabled( true );
		importBtn_->setEnabled( true );
		setStatus( QString( "Refreshed %1 Cacowards entries." ).arg( entries_.size() ) );
		return;
	}

	const CacowardEntry & entry = refreshEntries_[ refreshResolveIdx_ ];

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
		}
	}
	if (reply)
		reply->deleteLater();

	refreshResolveIdx_++;
	startNextRefreshResolution();
}

void CacowardsTab::saveDataFile()
{
	if (dataFilePath_.isEmpty())
		return;

	QJsonArray arr;
	for (const CacowardEntry & entry : refreshEntries_)
	{
		if (entry.dir.isEmpty() || entry.filename.isEmpty())
			continue;

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

void CacowardsTab::onItemChanged( QTreeWidgetItem * item, int column )
{
	if (column != 0 || updatingChecks_)
		return;

	updatingChecks_ = true;

	// checking/unchecking a year or category propagates to all descendant leaves
	if (item->childCount() > 0)
		setCheckStateRecursively( item, item->checkState( 0 ) );

	// reflect the new leaf states on the ancestors
	updateAncestorCheckStates( item->parent() );

	updatingChecks_ = false;

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
		const QString fileName = sanitizeFileName( entry.filename );
		const QString filePath = QDir( targetDir() ).filePath( fileName );

		downloads.append( PendingDownload{ filePath, entry.relativePath() } );
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
	const QString dir = targetDir();
	if (dir.isEmpty())
	{
		setStatus( "Choose a target directory first." );
		return;
	}

	if (!ensureTargetDirExists())
	{
		setStatus( "Could not create the target directory." );
		return;
	}

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
		}
		else
		{
			pendingDownloads_ = allDownloads;
		}
	}
	else
	{
		pendingDownloads_ = allDownloads;
	}

	if (pendingDownloads_.isEmpty())
	{
		setStatus( "Nothing to download (all selected files already exist)." );
		return;
	}

	batchActive_ = true;
	downloadCount_ = pendingDownloads_.size();
	downloadBtn_->setEnabled( false );

	startNextDownload();
}

void CacowardsTab::startNextDownload()
{
	if (pendingDownloads_.isEmpty())
	{
		batchActive_ = false;
		downloadCount_ = 0;
		progressBar_->setVisible( false );
		setStatus( "All downloads finished." );
		updateDownloadBtnState();
		return;
	}

	const PendingDownload next = pendingDownloads_.takeFirst();

	// build the candidate mirror URLs for this file, tried in order
	downloadUrls_.clear();
	for (const char * base : kDownloadMirrors)
		downloadUrls_.append( QString::fromLatin1( base ) + next.relativePath );
	downloadUrlIdx_ = 0;

	startDownload( next.filePath );
}

void CacowardsTab::startDownload( const QString & filePath )
{
	if (downloadUrlIdx_ >= downloadUrls_.size())
		return;

	const QString url = downloadUrls_[ downloadUrlIdx_ ];

	QUrl downloadUrl( url );
	QNetworkRequest request( downloadUrl );
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	downloadReply_ = network_->get( request );
	downloadPath_ = filePath;
	downloadFile_ = new QFile( filePath );
	if (!downloadFile_->open( QIODevice::WriteOnly ))
	{
		setStatus( "Could not open \"" + filePath + "\" for writing." );
		delete downloadFile_;
		downloadFile_ = nullptr;
		downloadPath_.clear();
		downloadReply_->abort();
		downloadReply_ = nullptr;
		startNextDownload();
		return;
	}

	// stream the incoming data directly to disk
	connect( downloadReply_, &QNetworkReply::readyRead, this, [ this ]()
	{
		if (downloadFile_ && downloadReply_)
			downloadFile_->write( downloadReply_->readAll() );
	});

	connect( downloadReply_, &QNetworkReply::downloadProgress, this, [ this ]( qint64 received, qint64 total )
	{
		if (total > 0)
		{
			progressBar_->setRange( 0, 100 );
			progressBar_->setValue( int( received * 100 / total ) );
		}
		else
		{
			progressBar_->setRange( 0, 0 );
		}
	});

	connect( downloadReply_, &QNetworkReply::finished, this, &CacowardsTab::onDownloadFinished );

	progressBar_->setVisible( true );
	progressBar_->setRange( 0, 0 );
	downloadBtn_->setEnabled( false );

	int currentIdx = downloadCount_ - pendingDownloads_.size();
	QString batchInfo = downloadCount_ > 0 ? QString( " [%1/%2]" ).arg( currentIdx ).arg( downloadCount_ ) : QString();
	setStatus( QString( "Downloading \"%1\"%2 (mirror %3/%4) ..." )
		.arg( QFileInfo( filePath ).fileName() )
		.arg( batchInfo )
		.arg( downloadUrlIdx_ + 1 )
		.arg( downloadUrls_.size() ) );
}

void CacowardsTab::onDownloadFinished()
{
	QNetworkReply * reply = downloadReply_;
	downloadReply_ = nullptr;

	// flush whatever remains in the reply's buffer
	if (downloadFile_)
	{
		if (reply)
			downloadFile_->write( reply->readAll() );
		downloadFile_->close();
	}

	progressBar_->setVisible( false );

	QString errorString;
	bool success = true;
	if (reply)
	{
		success = reply->error() == QNetworkReply::NoError;
		errorString = reply->errorString();
		reply->deleteLater();
	}

	delete downloadFile_;
	downloadFile_ = nullptr;

	const QString savedPath = downloadPath_;
	downloadPath_.clear();

	if (success)
	{
		setStatus( "Downloaded to \"" + savedPath + "\"." );
		emit downloadFinished( savedPath );
		startNextDownload();
		return;
	}

	// remove the partially-written file
	if (!savedPath.isEmpty())
		QFile::remove( savedPath );

	// try the next mirror, if there is one
	++downloadUrlIdx_;
	if (downloadUrlIdx_ < downloadUrls_.size())
	{
		setStatus( "Mirror failed (" + errorString + "), trying the next one ..." );
		startDownload( savedPath );
		return;
	}

	setStatus( "Download failed: " + errorString );
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

bool CacowardsTab::ensureTargetDirExists()
{
	const QString dir = targetDir();
	if (dir.isEmpty())
		return false;

	QDir target( dir );
	if (target.exists())
		return true;

	return target.mkpath( "." );
}

void CacowardsTab::setStatus( const QString & text )
{
	statusLabel_->setText( text );
}
