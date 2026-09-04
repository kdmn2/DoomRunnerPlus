//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Description: tab that lists WADs from the Doomworld "top lists" and lets the user download them from /idgames.
//======================================================================================================================

#include "TopWadsTab.hpp"

#include "Utils/ZipReader.hpp"       // extractZipArchive
#include "Utils/FileSystemUtils.hpp"  // isDirectoryWritable

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
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
#include <QJsonValue>
#include <QVector>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDateTime>
#include <QDesktopServices>
#include <QXmlStreamReader>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHash>
#include <QtGlobal>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <algorithm>
#include <random>
#include <utility>


//======================================================================================================================
// sources

namespace {

constexpr const char * kUserAgent = "DoomRunner/2.0 (top wads browser)";
constexpr const char * kIdgamesApiBase = "https://www.doomworld.com/idgames/api/api.php";
constexpr const char * kWaybackBase = "https://web.archive.org/web/2025/https://doomwiki.org/wiki/";

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

TopListSource makeSource( QString umbrella, QString pageTitle,
                          TopListSource::Grouping grouping, QString part = QString() )
{
	TopListSource s;
	s.umbrella = std::move( umbrella );
	s.pageTitle = std::move( pageTitle );
	s.grouping = grouping;
	s.part = std::move( part );
	return s;
}

const QVector< TopListSource > & topListSources()
{
	static const QVector< TopListSource > sources = {
		makeSource( "Top 100 WADs of All Time",  "Top 100 WADs of All Time",  TopListSource::Grouping::Year ),
		makeSource( "Top 100 Most Memorable Maps","Top 100 Most Memorable Maps",TopListSource::Grouping::Flat ),
		makeSource( "Top Missed Cacowards",       "Top 25 Missed Cacowards",  TopListSource::Grouping::Part, "Part 1" ),
		makeSource( "Top Missed Cacowards",       "Missed Cacowards 2",       TopListSource::Grouping::Part, "Part 2" ),
	};
	return sources;
}

} // namespace

const TopListSource & getTopListSource( int idx ) { return topListSources().at( idx ); }
int topListSourceCount() { return int( topListSources().size() ); }


//======================================================================================================================

TopWadsTab::TopWadsTab( QWidget * parent )
	: QWidget( parent ),
	  network_( new QNetworkAccessManager( this ) )
{
	buildUi();
}

TopWadsTab::~TopWadsTab()
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

void TopWadsTab::setTargetDir( const QString & dir ) { targetDirLine_->setText( dir ); }
QString TopWadsTab::targetDir() const { return targetDirLine_->text().trimmed(); }

void TopWadsTab::setAutosort( bool enabled ) { autosortChk_->setChecked( enabled ); }
bool TopWadsTab::autosort() const { return autosortChk_->isChecked(); }
void TopWadsTab::setUnpack( bool enabled ) { unpackChk_->setChecked( enabled ); deleteAfterExtractChk_->setEnabled( enabled ); }
bool TopWadsTab::unpack() const { return unpackChk_->isChecked(); }
void TopWadsTab::setDeleteAfterExtract( bool enabled ) { deleteAfterExtractChk_->setChecked( enabled ); }
bool TopWadsTab::deleteAfterExtract() const { return deleteAfterExtractChk_->isChecked(); }

void TopWadsTab::setDataFilePath( const QString & path )
{
	dataFilePath_ = path;
	loadData();
}

QStringList TopWadsTab::expandedNodes() const
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

void TopWadsTab::setExpandedNodes( const QStringList & nodes ) { expandedNodes_ = nodes; }

void TopWadsTab::setParallelDownload( bool enabled )
{
	maxParallel_ = enabled ? 4 : 1;
	if (parallelChk_)
		parallelChk_->setChecked( enabled );
}

bool TopWadsTab::parallelDownload() const { return maxParallel_ > 1; }

void TopWadsTab::setMapSourceDir( const QString & dir ) { mapSourceDir_ = dir; }

//----------------------------------------------------------------------------------------------------------------------

void TopWadsTab::buildUi()
{
	//-- top row: refresh + import ------------------------------------------------

	refreshBtn_ = new QPushButton( "Refresh", this );
	refreshBtn_->setToolTip( "Re-download the list from doomwiki (via the Wayback Machine)" );

	generateListBtn_ = new QPushButton( "Generate list", this );
	generateListBtn_->setToolTip( "Open doomwiki's export page in your browser so you can download the XML yourself" );

	importBtn_ = new QPushButton( "Import...", this );
	importBtn_->setToolTip( "Load the list from a doomwiki export XML file (use the doomwiki 'Export' page)" );

	QHBoxLayout * refreshRow = new QHBoxLayout;
	refreshRow->addStretch( 1 );
	refreshRow->addWidget( refreshBtn_ );
	refreshRow->addWidget( generateListBtn_ );
	refreshRow->addWidget( importBtn_ );

	tree_ = new QTreeWidget( this );
	tree_->setColumnCount( 1 );
	tree_->setHeaderLabel( "WAD" );
	tree_->setSelectionMode( QAbstractItemView::SingleSelection );

	detailsView_ = new QPlainTextEdit( this );
	detailsView_->setReadOnly( true );
	detailsView_->setPlaceholderText( "Select an entry to see its details." );
	detailsView_->setMinimumHeight( 100 );

	QWidget * treePanel = new QWidget( this );
	QVBoxLayout * treeLayout = new QVBoxLayout( treePanel );
	treeLayout->setContentsMargins( 0, 0, 0, 0 );
	treeLayout->addLayout( refreshRow );
	treeLayout->addWidget( tree_, 1 );
	treeLayout->addWidget( detailsView_ );

	//-- bottom row: target dir + download --------------------------------------

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

	//-- download options --------------------------------------------------------

	autosortChk_ = new QCheckBox( "Autosort", this );
	autosortChk_->setToolTip( "Save downloads into \"<source>/<year-or-part>\" subfolders" );

	unpackChk_ = new QCheckBox( "Unpack", this );
	unpackChk_->setToolTip( "Extract each downloaded ZIP into a folder named after the archive" );

	deleteAfterExtractChk_ = new QCheckBox( "Delete after extracting", this );
	deleteAfterExtractChk_->setToolTip( "Delete the original archive after it has been unpacked" );
	deleteAfterExtractChk_->setEnabled( false );

	parallelChk_ = new QCheckBox( "Download 4 at a time", this );
	parallelChk_->setToolTip( "Download up to 4 files at the same time" );
	parallelChk_->setChecked( maxParallel_ > 1 );

	QHBoxLayout * optionsRow = new QHBoxLayout;
	optionsRow->addStretch( 1 );
	optionsRow->addWidget( autosortChk_ );
	optionsRow->addWidget( unpackChk_ );
	optionsRow->addWidget( deleteAfterExtractChk_ );
	optionsRow->addWidget( parallelChk_ );

	//-- activity log ------------------------------------------------------------

	logChk_ = new QCheckBox( "Show log", this );
	logChk_->setToolTip( "Show a log of list loading/importing, downloads and extraction events" );

	logView_ = new QPlainTextEdit( this );
	logView_->setReadOnly( true );
	logView_->setPlaceholderText( "No activity logged yet." );
	logView_->setMinimumHeight( 140 );
	logView_->setVisible( false );

	QHBoxLayout * logRow = new QHBoxLayout;
	logRow->addStretch( 1 );
	logRow->addWidget( logChk_ );

	//-- status bar --------------------------------------------------------------

	statusLabel_ = new QLabel( "Loading...", this );
	statusLabel_->setWordWrap( true );

	progressBar_ = new QProgressBar( this );
	progressBar_->setVisible( false );
	progressBar_->setRange( 0, 0 );

	//-- assemble ---------------------------------------------------------------

	QVBoxLayout * mainLayout = new QVBoxLayout( this );
	mainLayout->addWidget( treePanel, 1 );
	mainLayout->addLayout( downloadRow );
	mainLayout->addLayout( optionsRow );
	mainLayout->addLayout( logRow );
	mainLayout->addWidget( logView_ );
	mainLayout->addWidget( progressBar_ );
	mainLayout->addWidget( statusLabel_ );

	//-- signal/slot wiring ------------------------------------------------------

	connect( refreshBtn_, &QPushButton::clicked, this, &TopWadsTab::refresh );
	connect( generateListBtn_, &QPushButton::clicked, this, &TopWadsTab::generateList );
	connect( importBtn_, &QPushButton::clicked, this, &TopWadsTab::importExportedXml );
	connect( tree_, &QTreeWidget::currentItemChanged, this, &TopWadsTab::onCurrentItemChanged );
	connect( tree_, &QTreeWidget::itemChanged, this, &TopWadsTab::onItemChanged );
	connect( tree_, &QTreeWidget::itemExpanded, this, [ this ]( QTreeWidgetItem * ){ onExpansionChanged(); } );
	connect( tree_, &QTreeWidget::itemCollapsed, this, [ this ]( QTreeWidgetItem * ){ onExpansionChanged(); } );
	connect( browseBtn_, &QPushButton::clicked, this, &TopWadsTab::browseTargetDir );
	connect( downloadBtn_, &QPushButton::clicked, this, &TopWadsTab::downloadChecked );

	connect( targetDirLine_, &QLineEdit::textChanged, this, [ this ]( const QString & text )
	{
		emit targetDirChanged( text.trimmed() );
	});

	connect( autosortChk_, &QCheckBox::toggled, this, &TopWadsTab::autosortChanged );
	connect( unpackChk_, &QCheckBox::toggled, this, &TopWadsTab::unpackChanged );
	connect( deleteAfterExtractChk_, &QCheckBox::toggled, this, &TopWadsTab::deleteAfterExtractChanged );
	connect( parallelChk_, &QCheckBox::toggled, this, [ this ]( bool enabled )
	{
		setParallelDownload( enabled );
		emit parallelDownloadChanged( enabled );
	});
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

void TopWadsTab::loadData()
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
		entries_.clear();
		setStatus( "No data available yet. Click \"Refresh\" or import a doomwiki export XML." );
		logMessage( "No data available yet. Click Refresh or import a doomwiki export XML." );
		buildTree();
		return;
	}

	entries_.clear();
	parseExportXml( json );
	buildTree( /*restoreExpansion*/ true );
	setStatus( QString( "Loaded %1 entries (%2)." ).arg( entries_.size() ).arg( sourceDesc ) );
	logMessage( QString( "Loaded %1 entries from %2." ).arg( entries_.size() ).arg( sourceDesc ) );
}

void TopWadsTab::parseExportXml( const QByteArray & xml )
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
					for (int i = 0; i < topListSourceCount(); ++i)
					{
						const TopListSource & src = getTopListSource( i );
						if (pageTitle == src.pageTitle)
						{
							parseSourceWikitext( src, pageText );
							break;
						}
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

// Parses one source page's wikitext into entries (umbrella, subgroup, title, download reference).
void TopWadsTab::parseSourceWikitext( const TopListSource & src, const QString & wikitext )
{
	static const QRegularExpression headingRe( QStringLiteral("^\\s*(={2,6})\\s*(.+?)\\s*\\1\\s*$") );
	static const QRegularExpression listBase( QStringLiteral("^\\s*[#*]\\s*\\[\\[([^\\]|]+)(?:\\|([^\\]]+))?\\]\\]") );
	static const QRegularExpression idRe( QStringLiteral("\\{\\{ig\\|id=(\\d+)") );
	static const QRegularExpression fileRe( QStringLiteral("\\{\\{ig\\|file=([^}|]+)") );

	bool inList = false;
	QString currentYear;

	const QStringList lines = wikitext.split( '\n' );
	for (const QString & line : lines)
	{
		const auto hm = headingRe.match( line );
		if (hm.hasMatch())
		{
			const int level = hm.captured( 1 ).length();
			const QString text = hm.captured( 2 ).trimmed();
			if (level == 2)  // section boundary (== The list ==, == See also ==, ...)
			{
				inList = (text == QLatin1String("The list"));
				if (!inList)
					currentYear.clear();
			}
			else if (level == 3 && inList)  // year heading like ===1994===
			{
				bool ok = false;
				text.toInt( &ok );
				if (ok)
					currentYear = text;
			}
			continue;
		}

		if (!inList)
			continue;

		const auto tm = listBase.match( line );
		if (!tm.hasMatch())
			continue;

		TopWadEntry entry;
		entry.umbrella = src.umbrella;
		entry.title = tm.captured( 2 ).isEmpty() ? tm.captured( 1 ).trimmed() : tm.captured( 2 ).trimmed();
		if (entry.title.isEmpty())
			continue;

		if (src.grouping == TopListSource::Grouping::Year)
			entry.subgroup = currentYear;
		else if (src.grouping == TopListSource::Grouping::Part)
			entry.subgroup = src.part;
		else
			entry.subgroup.clear();

		const auto idm = idRe.match( line );
		const auto fm  = fileRe.match( line );
		if (idm.hasMatch())
		{
			entry.id = idm.captured( 1 ).toInt();
		}
		else if (fm.hasMatch())
		{
			const QString file = fm.captured( 1 ).trimmed();
			const int slash = file.lastIndexOf( '/' );
			entry.dir = slash >= 0 ? file.left( slash + 1 ) : QString();
			entry.filename = slash >= 0 ? file.mid( slash + 1 ) : file;
			if (!entry.filename.contains( '.' ))
				entry.filename += QLatin1String(".zip");
		}
		// entries without an ig reference are listed but not downloadable

		entries_.append( entry );
	}
}

void TopWadsTab::buildTree( bool restoreExpansion )
{
	tree_->clear();

	QHash< QString, QTreeWidgetItem * > umbrellaNodes;
	QHash< QPair< QString, QString >, QTreeWidgetItem * > subNodes;

	for (int i = 0; i < entries_.size(); ++i)
	{
		const TopWadEntry & entry = entries_[ i ];

		QTreeWidgetItem * umbrella = umbrellaNodes.value( entry.umbrella, nullptr );
		if (!umbrella)
		{
			umbrella = new QTreeWidgetItem( tree_, QStringList{ entry.umbrella } );
			umbrella->setFlags( umbrella->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
			umbrella->setCheckState( 0, Qt::Unchecked );
			umbrellaNodes.insert( entry.umbrella, umbrella );
		}

		QTreeWidgetItem * parent = umbrella;
		if (!entry.subgroup.isEmpty())
		{
			const auto key = qMakePair( entry.umbrella, entry.subgroup );
			QTreeWidgetItem * sub = subNodes.value( key, nullptr );
			if (!sub)
			{
				sub = new QTreeWidgetItem( umbrella, QStringList{ entry.subgroup } );
				sub->setFlags( sub->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
				sub->setCheckState( 0, Qt::Unchecked );
				subNodes.insert( key, sub );
			}
			parent = sub;
		}

		auto * leaf = new QTreeWidgetItem( parent, QStringList{ entry.title } );
		leaf->setFlags( leaf->flags() | Qt::ItemIsUserCheckable );
		leaf->setCheckState( 0, Qt::Unchecked );
		leaf->setData( 0, Qt::UserRole, i );
	}

	if (restoreExpansion)
	{
		restoringExpansion_ = true;
		applyExpansionState();
		restoringExpansion_ = false;
	}
}

void TopWadsTab::applyExpansionState()
{
	for (int i = 0; i < tree_->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem * umbrella = tree_->topLevelItem( i );
		const QString umbrellaPath = umbrella->text( 0 );
		if (expandedNodes_.contains( umbrellaPath ))
			umbrella->setExpanded( true );

		for (int c = 0; c < umbrella->childCount(); ++c)
		{
			QTreeWidgetItem * sub = umbrella->child( c );
			const QString subPath = umbrellaPath + '/' + sub->text( 0 );
			if (expandedNodes_.contains( subPath ))
				sub->setExpanded( true );
		}
	}
}

void TopWadsTab::onExpansionChanged()
{
	if (restoringExpansion_)
		return;
	emit expansionChanged();
}

//----------------------------------------------------------------------------------------------------------------------
// list refresh (fetch each source page via the Wayback Machine)

void TopWadsTab::refresh()
{
	if (refreshReply_)
		refreshReply_->abort();

	refreshSourceIdx_ = 0;
	refreshData_.clear();

	refreshBtn_->setEnabled( false );
	importBtn_->setEnabled( false );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, 0 );
	setStatus( "Refreshing the list..." );
	logMessage( "Refreshing the list from doomwiki (via the Wayback Machine)..." );

	startNextRefresh();
}

void TopWadsTab::startNextRefresh()
{
	if (refreshSourceIdx_ >= topListSourceCount())
	{
		// all sources fetched, build and save the list
		saveDataFile();
		buildTree();
		emit expansionChanged();

		progressBar_->setVisible( false );
		refreshBtn_->setEnabled( true );
		importBtn_->setEnabled( true );
		setStatus( QString( "Loaded %1 entries and saved the list." ).arg( entries_.size() ) );
		logMessage( QString( "Done: fetched all sources, %1 entries." ).arg( entries_.size() ) );
		return;
	}

	const TopListSource & src = getTopListSource( refreshSourceIdx_ );
	const QString urlString = QString::fromLatin1( kWaybackBase ) + src.pageTitle;
	QUrl url( urlString );

	QNetworkRequest request( url );
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	logMessage( "Fetching " + urlString );
	refreshReply_ = network_->get( request );
	connect( refreshReply_, &QNetworkReply::finished, this, [ this, src ]()
	{
		QNetworkReply * reply = refreshReply_;
		refreshReply_ = nullptr;
		if (reply)
		{
			if (reply->error() == QNetworkReply::NoError)
			{
				const QString html = QString::fromUtf8( reply->readAll() );
				const int before = entries_.size();
				parseSourceWikitext( src, html );
				logMessage( QString( "Fetched \"%1\" (%2 entries)." ).arg( src.umbrella ).arg( entries_.size() - before ) );
			}
			else
			{
				logMessage( QString( "Failed to fetch \"%1\": %2" ).arg( src.umbrella ).arg( reply->errorString() ) );
			}
			reply->deleteLater();
		}
		refreshSourceIdx_++;
		startNextRefresh();
	});
}

void TopWadsTab::importExportedXml()
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

	entries_.clear();
	parseExportXml( file.readAll() );

	if (entries_.isEmpty())
	{
		setStatus( "No entries found in \"" + filePath + "\"." );
		logMessage( "Import failed: no entries found in \"" + filePath + "\"." );
		return;
	}

	saveDataFile();
	buildTree();
	emit expansionChanged();
	setStatus( QString( "Imported %1 entries from \"%2\"." ).arg( entries_.size() ).arg( filePath ) );
	logMessage( QString( "Imported %1 entries from \"%2\"." ).arg( entries_.size() ).arg( filePath ) );
}

void TopWadsTab::generateList()
{
	openBrowserExportPage();
	setStatus( "Opened doomwiki's export page in your browser - click \"Export\" there to download the XML file, then press \"Import...\" here to load it." );
	logMessage( "Opened doomwiki's export page in your browser." );
}

void TopWadsTab::openBrowserExportPage()
{
	// open doomwiki's Special:Export page in the user's browser with all source pages pre-listed
	QStringList pageNames;
	for (int i = 0; i < topListSourceCount(); ++i)
		pageNames.append( getTopListSource( i ).pageTitle );

	QUrl url( QStringLiteral("https://doomwiki.org/w/index.php") );
	QUrlQuery query;
	query.addQueryItem( "title", "Special:Export" );
	query.addQueryItem( "pages", pageNames.join( '\n' ) );
	url.setQuery( query );

	QDesktopServices::openUrl( url );
}

void TopWadsTab::saveDataFile()
{
	if (dataFilePath_.isEmpty())
		return;

	QJsonArray arr;
	for (const TopWadEntry & entry : entries_)
	{
		QJsonObject obj;
		obj[ "umbrella" ]  = entry.umbrella;
		obj[ "subgroup" ]  = entry.subgroup;
		obj[ "title" ]     = entry.title;
		obj[ "id" ]        = entry.id;
		obj[ "dir" ]       = entry.dir;
		obj[ "filename" ]  = entry.filename;
		arr.append( obj );
	}

	QFile file( dataFilePath_ );
	if (file.open( QIODevice::WriteOnly ))
	{
		file.write( QJsonDocument( arr ).toJson( QJsonDocument::Indented ) );
		file.close();
		logMessage( "Saved the list to \"" + dataFilePath_ + "\"." );
	}
}

//----------------------------------------------------------------------------------------------------------------------
// tree interaction

void TopWadsTab::onCurrentItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * /*previous*/ )
{
	if (!current || current->childCount() != 0)
	{
		detailsView_->clear();
		return;
	}

	const int idx = current->data( 0, Qt::UserRole ).toInt();
	if (idx < 0 || idx >= entries_.size())
		return;

	const TopWadEntry & entry = entries_[ idx ];

	QString text;
	text += "Title: " + entry.title + '\n';
	text += "Source: " + entry.umbrella + '\n';
	if (!entry.subgroup.isEmpty())
		text += "Sub-group: " + entry.subgroup + '\n';
	text += "File: " + entry.filename + '\n';

	detailsView_->setPlainText( text );
}

void TopWadsTab::onItemChanged( QTreeWidgetItem * /*item*/, int column )
{
	if (column != 0)
		return;
	updateDownloadBtnState();
}

//----------------------------------------------------------------------------------------------------------------------
// downloading

void TopWadsTab::browseTargetDir()
{
	const QString dir = QFileDialog::getExistingDirectory( this, "Select download directory", targetDir() );
	if (dir.isEmpty())
		return;
	targetDirLine_->setText( dir );
}

void TopWadsTab::updateDownloadBtnState()
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

QList< TopWadsTab::PendingDownload > TopWadsTab::collectCheckedDownloads() const
{
	QList< PendingDownload > downloads;

	QTreeWidgetItemIterator it( tree_, QTreeWidgetItemIterator::Checked );
	while (*it)
	{
		QTreeWidgetItem * item = *it;
		++it;

		if (item->childCount() != 0)
			continue;

		const int idx = item->data( 0, Qt::UserRole ).toInt();
		if (idx < 0 || idx >= entries_.size())
			continue;

		const TopWadEntry & entry = entries_[ idx ];

		// Entries carrying only an /idgames id are resolved lazily at download time.
		if (entry.filename.isEmpty())
		{
			if (entry.id != 0)
				downloads.append( PendingDownload{ QString(), QString(), entry.title, entry.id, entry.umbrella, entry.subgroup } );
			continue;
		}

		const QString fileName = sanitizeFileName( entry.filename );

		QString subDir;
		if (autosortChk_->isChecked())
		{
			subDir = sanitizeFileName( entry.umbrella );
			if (!entry.subgroup.isEmpty())
				subDir += '/' + sanitizeFileName( entry.subgroup );
		}

		const QString filePath = QDir( targetDir() ).filePath( subDir.isEmpty() ? fileName : subDir + '/' + fileName );
		downloads.append( PendingDownload{ filePath, entry.relativePath(), entry.title, 0, entry.umbrella, entry.subgroup } );
	}

	return downloads;
}

void TopWadsTab::downloadChecked()
{
	if (!ensureTargetDirUsable())
		return;

	QList< PendingDownload > allDownloads = collectCheckedDownloads();
	if (allDownloads.isEmpty())
	{
		setStatus( "Check the entries you want to download." );
		return;
	}

	QStringList existingNames;
	for (const PendingDownload & d : allDownloads)
		if (QFile::exists( d.filePath ))
			existingNames.append( QFileInfo( d.filePath ).fileName() );

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

	logMessage( QString( "Starting download of %1 file(s) to \"%2\"." ).arg( pendingDownloads_.size() ).arg( targetDir() ) );

	batchActive_ = true;
	downloadCount_ = pendingDownloads_.size();
	downloadBtn_->setEnabled( false );

	startNextDownload();
}

void TopWadsTab::startNextDownload()
{
	while (activeDownloads_.size() < size_t( maxParallel_ ) && !pendingDownloads_.isEmpty())
	{
		const PendingDownload next = pendingDownloads_.takeFirst();

		auto download = std::make_unique< ActiveDownload >();
		download->path = next.filePath;
		download->title = next.title;
		download->resolveId = next.id;
		download->umbrella = next.umbrella;
		download->subgroup = next.subgroup;
		if (next.id == 0)
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

void TopWadsTab::startDownload( ActiveDownload * download )
{
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

		logMessage( QString( "Resolving /idgames id %1 via %2" ).arg( download->resolveId ).arg( url.toString() ) );
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

	logMessage( QString( "Downloading \"%1\" [%2/%3] via %4" )
		.arg( QFileInfo( download->path ).fileName() )
		.arg( download->urlIdx + 1 )
		.arg( download->urls.size() )
		.arg( url ) );

	QDir().mkpath( QFileInfo( download->path ).absolutePath() );

	QFile * file = new QFile( download->path );
	if (!file->open( QIODevice::WriteOnly ))
	{
		setStatus( "Could not open \"" + download->path + "\" for writing." );
		logMessage( "Download failed: could not open \"" + download->path + "\" for writing." );
		delete file;
		removeDownload( download );
		startNextDownload();
		return;
	}
	download->file = file;

	download->reply = network_->get( request );
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
}

void TopWadsTab::onDownloadIdResolved( ActiveDownload * download )
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
		const QString fileName = sanitizeFileName( filename );

		QString subDir;
		if (autosortChk_->isChecked())
		{
			subDir = sanitizeFileName( download->umbrella );
			if (!download->subgroup.isEmpty())
				subDir += '/' + sanitizeFileName( download->subgroup );
		}
		download->path = QDir( targetDir() ).filePath( subDir.isEmpty() ? fileName : subDir + '/' + fileName );

		const QString relativePath = dir + fileName;
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

void TopWadsTab::onDownloadFinished( ActiveDownload * download )
{
	QNetworkReply * reply = download->reply;
	download->reply = nullptr;

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

		if (QFileInfo( savedPath ).size() <= 0)
		{
			success = false;
			errorString = "downloaded file is empty";
		}
	}

	if (success)
	{
		logMessage( QString( "Downloaded \"%1\" (%2 bytes) from %3" )
			.arg( savedPath ).arg( QFileInfo( savedPath ).size() )
			.arg( download->urls.value( download->urlIdx - 1 ) ) );

		if (unpackChk_->isChecked())
		{
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
			return;
		}

		setStatus( "Downloaded to \"" + savedPath + "\"." );
		emit downloadFinished( savedPath );
	}
	else
	{
		if (errorString.isEmpty())
			errorString = "download failed";
		if (!savedPath.isEmpty())
			QFile::remove( savedPath );
		if (download->file)
		{
			download->file->deleteLater();
			download->file = nullptr;
		}

		++download->urlIdx;
		if (download->urlIdx < download->urls.size())
		{
			setStatus( "Mirror failed (" + errorString + "), trying the next one ..." );
			logMessage( QString( "Mirror failed for \"%1\" at %2 (%3); trying the next mirror." )
				.arg( QFileInfo( savedPath ).fileName() )
				.arg( download->urls.value( download->urlIdx ) )
				.arg( errorString ) );
			startDownload( download );
			return;
		}

		setStatus( "Download failed: " + errorString );
		logMessage( QString( "Failed to download \"%1\": %2 (last URL: %3) - all %4 mirror(s) failed." )
			.arg( QFileInfo( savedPath ).fileName() )
			.arg( errorString )
			.arg( download->urls.value( download->urlIdx ) )
			.arg( download->urls.size() ) );
	}

	updateDownloadProgress();
	removeDownload( download );
	startNextDownload();
}

void TopWadsTab::onUnpackFinished( ActiveDownload * download )
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

QString TopWadsTab::sanitizeFileName( const QString & fileName ) const
{
	QString name = fileName;
	name = name.section( '/', -1 ).section( '\\', -1 );
	name.remove( QRegularExpression( "[<>:\"/\\\\|?*\\x00-\\x1f]" ) );
	if (name.isEmpty())
		name = "download";
	return name;
}

bool TopWadsTab::ensureTargetDirUsable()
{
	QString dir = targetDir();

	if (dir.isEmpty() || !QFileInfo::exists( dir ) || !fs::isDirectoryWritable( dir ))
	{
		if (askToUseMapDir( dir ))
			return true;
		dir = targetDir();
	}

	if (dir.isEmpty())
	{
		setStatus( "Choose a target directory first." );
		return false;
	}

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

	if (!fs::isDirectoryWritable( dir ))
	{
		QMessageBox::warning( this, "Target directory not usable",
			"Target directory \"" + dir + "\" is not accessible for writing.\nPlease choose another directory." );
		logMessage( "Target directory \"" + dir + "\" is not writable." );
		return false;
	}

	return true;
}

bool TopWadsTab::askToUseMapDir( const QString & targetDir )
{
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
			targetDirLine_->setText( mapSourceDir_ );
			logMessage( "Switched the download target to the map directory \"" + mapSourceDir_ + "\"." );
			return true;
		}
	}
	return false;
}

void TopWadsTab::removeDownload( ActiveDownload * download )
{
	for (size_t i = 0; i < activeDownloads_.size(); ++i)
		if (activeDownloads_[ i ].get() == download)
		{
			activeDownloads_.erase( activeDownloads_.begin() + qsizetype( i ) );
			return;
		}
}

void TopWadsTab::updateDownloadProgress()
{
	if (downloadCount_ <= 0)
		return;
	const int completed = downloadCount_ - pendingDownloads_.size() - int( activeDownloads_.size() );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, downloadCount_ );
	progressBar_->setValue( qBound( 0, completed, downloadCount_ ) );
}

void TopWadsTab::setStatus( const QString & text ) { statusLabel_->setText( text ); }

void TopWadsTab::logMessage( const QString & message )
{
	logView_->appendPlainText( QString( "[%1] %2" )
		.arg( QDateTime::currentDateTime().toString( "HH:mm:ss" ) )
		.arg( message ) );
	if (QScrollBar * bar = logView_->verticalScrollBar())
		bar->setValue( bar->maximum() );
}
