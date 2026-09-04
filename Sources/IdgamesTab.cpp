//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      (idgames browser feature)
// Description: tab for searching and downloading WADs/ZIPs from the Doomworld /idgames archive
//======================================================================================================================

#include "IdgamesTab.hpp"

#include "Utils/FileSystemUtils.hpp"  // isDirectoryWritable
#include "Utils/ZipReader.hpp"        // extractZipArchive

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <algorithm>

#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QDateTime>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QVBoxLayout>


//======================================================================================================================
// constants

namespace {

/// Base URL of the public /idgames HTTP API, see https://www.doomworld.com/idgames/api/
constexpr const char * kApiBaseUrl = "https://www.doomworld.com/idgames/api/api.php";

constexpr const char * kUserAgent = "DoomRunner/2.0 (idgames browser)";

/// Download mirrors of the /idgames archive, tried in order.
/** The primary doomworld.com URL 301-redirects to legacy.doomworld.com whose TLS
  * certificate has expired, so we use direct mirrors that serve the raw files instead. */
constexpr const char * kDownloadMirrors [] = {
	"https://youfailit.net/pub/idgames/",
	"https://ftp.gamers.org/pub/idgames/",
};

/// Formats a size in bytes into a short human-readable string.
QString formatSize( qint64 bytes )
{
	if (bytes < 1024)
		return QString::number( bytes ) + " B";

	double value = bytes;
	const char * units [] = { "KiB", "MiB", "GiB", "TiB" };
	int unitIdx = -1;
	do {
		value /= 1024.0;
		unitIdx++;
	} while (value >= 1024.0 && unitIdx < 3);

	return QString::number( value, 'f', 1 ) + ' ' + units[ unitIdx ];
}

/// Formats a Unix timestamp (seconds since epoch) into an approximate age like "35 years".
QString formatAge( qint64 age )
{
	if (age <= 0)
		return QString();

	// different lengths for leap years are irrelevant at this precision; 365.25 is a good enough average
	constexpr double secondsPerYear = 365.25 * 24.0 * 3600.0;
	const qint64 years = qMax< qint64 >( 0, ( QDateTime::currentSecsSinceEpoch() - age ) / qint64( secondsPerYear ) );

	return QStringLiteral( "%1 year%2" ).arg( years ).arg( years == 1 ? QString() : QStringLiteral("s") );
}

} // namespace


//======================================================================================================================

IdgamesTab::IdgamesTab( QWidget * parent )
	: QWidget( parent ),
	  network_( new QNetworkAccessManager( this ) )
{
	buildUi();
}

IdgamesTab::~IdgamesTab()
{
	if (searchReply_)
		searchReply_->abort();
	for (const auto & download : activeDownloads_)
	{
		if (download->reply)
			download->reply->abort();
		delete download->file;
	}
}

void IdgamesTab::setTargetDir( const QString & dir )
{
	targetDirLine_->setText( dir );
}

QString IdgamesTab::targetDir() const
{
	return targetDirLine_->text().trimmed();
}

void IdgamesTab::setMapSourceDir( const QString & dir )
{
	mapSourceDir_ = dir;
}

void IdgamesTab::setAutosort( bool enabled ) { autosortChk_->setChecked( enabled ); }
bool IdgamesTab::autosort() const { return autosortChk_->isChecked(); }
void IdgamesTab::setUnpack( bool enabled ) { unpackChk_->setChecked( enabled ); deleteAfterExtractChk_->setEnabled( enabled ); }
bool IdgamesTab::unpack() const { return unpackChk_->isChecked(); }
void IdgamesTab::setDeleteAfterExtract( bool enabled ) { deleteAfterExtractChk_->setChecked( enabled ); }
bool IdgamesTab::deleteAfterExtract() const { return deleteAfterExtractChk_->isChecked(); }

void IdgamesTab::setParallelDownload( bool enabled )
{
	maxParallel_ = enabled ? 4 : 1;
	if (parallelChk_)
		parallelChk_->setChecked( enabled );
}

bool IdgamesTab::parallelDownload() const { return maxParallel_ > 1; }

//----------------------------------------------------------------------------------------------------------------------

void IdgamesTab::buildUi()
{
	//-- top row: search controls -----------------------------------------------

	searchLine_ = new QLineEdit( this );
	searchLine_->setPlaceholderText( "Search /idgames (at least 3 characters, use * as wildcard)" );
	searchLine_->setClearButtonEnabled( true );

	searchBtn_ = new QPushButton( "Search", this );
	searchBtn_->setFixedWidth( 110 );

	showAllBtn_ = new QPushButton( "Show All", this );
	showAllBtn_->setFixedWidth( 110 );

	typeCmb_ = new QComboBox( this );
	typeCmb_->addItem( "title",      QStringLiteral("title") );
	typeCmb_->addItem( "filename",   QStringLiteral("filename") );
	typeCmb_->addItem( "author",     QStringLiteral("author") );
	typeCmb_->addItem( "description", QStringLiteral("description") );
	typeCmb_->addItem( "text file",  QStringLiteral("textfile") );

	sortCmb_ = new QComboBox( this );
	sortCmb_->addItem( "date",     QStringLiteral("date") );
	sortCmb_->addItem( "filename", QStringLiteral("filename") );
	sortCmb_->addItem( "size",     QStringLiteral("size") );
	sortCmb_->addItem( "rating",   QStringLiteral("rating") );

	dirCmb_ = new QComboBox( this );
	dirCmb_->addItem( "ascending",  QStringLiteral("asc") );
	dirCmb_->addItem( "descending", QStringLiteral("desc") );
	dirCmb_->setCurrentIndex( 1 );  // newest / best first by default

	QHBoxLayout * searchRow = new QHBoxLayout;
	searchRow->addWidget( searchLine_, 1 );
	searchRow->addWidget( searchBtn_ );
	searchRow->addWidget( showAllBtn_ );
	searchRow->addSpacing( 10 );
	searchRow->addWidget( new QLabel( "In:", this ) );
	searchRow->addWidget( typeCmb_ );
	searchRow->addWidget( new QLabel( "Sort:", this ) );
	searchRow->addWidget( sortCmb_ );
	searchRow->addWidget( dirCmb_ );

	//-- results table and details ---------------------------------------------

	resultsTable_ = new QTableWidget( this );
	resultsTable_->setColumnCount( 8 );
	resultsTable_->setHorizontalHeaderLabels( QStringList{ "✓", "Title", "Author", "Rating", "Votes", "Size", "Date", "Age" } );
	resultsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
	resultsTable_->setSelectionMode( QAbstractItemView::SingleSelection );
	resultsTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
	resultsTable_->verticalHeader()->setVisible( false );
	resultsTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	resultsTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
	resultsTable_->horizontalHeader()->setContextMenuPolicy( Qt::CustomContextMenu );

	detailsView_ = new QTextEdit( this );
	detailsView_->setReadOnly( true );
	detailsView_->setPlaceholderText( "Select an entry to see its description." );
	detailsView_->setMinimumHeight( 120 );

	QWidget * resultsPanel = new QWidget( this );
	QVBoxLayout * resultsLayout = new QVBoxLayout( resultsPanel );
	resultsLayout->setContentsMargins( 0, 0, 0, 0 );
	resultsLayout->addWidget( resultsTable_, 1 );
	resultsLayout->addWidget( detailsView_ );

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
	autosortChk_->setToolTip( "Save downloads into an \"IdGames\" subfolder" );

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

	//-- status bar -------------------------------------------------------------

	statusLabel_ = new QLabel( "Enter a search query or click \"Show All\".", this );
	statusLabel_->setWordWrap( true );

	progressBar_ = new QProgressBar( this );
	progressBar_->setVisible( false );
	progressBar_->setRange( 0, 0 );  // busy indicator by default

	//-- assemble ---------------------------------------------------------------

	QVBoxLayout * mainLayout = new QVBoxLayout( this );
	mainLayout->addLayout( searchRow );
	mainLayout->addWidget( resultsPanel, 1 );
	mainLayout->addLayout( downloadRow );
	mainLayout->addLayout( optionsRow );
	mainLayout->addWidget( progressBar_ );
	mainLayout->addWidget( statusLabel_ );

	//-- signal/slot wiring -----------------------------------------------------

	connect( searchBtn_, &QPushButton::clicked, this, &IdgamesTab::search );
	connect( showAllBtn_, &QPushButton::clicked, this, &IdgamesTab::showAll );
	connect( searchLine_, &QLineEdit::returnPressed, this, &IdgamesTab::search );
	connect( resultsTable_, &QTableWidget::currentCellChanged, this, &IdgamesTab::onCurrentRowChanged );
	connect( resultsTable_, &QTableWidget::itemChanged, this, &IdgamesTab::onItemChanged );
	connect( browseBtn_, &QPushButton::clicked, this, &IdgamesTab::browseTargetDir );
	connect( downloadBtn_, &QPushButton::clicked, this, &IdgamesTab::downloadChecked );
	connect( resultsTable_->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, &IdgamesTab::onHeaderContextMenu );

	// download option toggles
	connect( autosortChk_, &QCheckBox::toggled, this, &IdgamesTab::autosortChanged );
	connect( unpackChk_, &QCheckBox::toggled, this, &IdgamesTab::unpackChanged );
	connect( deleteAfterExtractChk_, &QCheckBox::toggled, this, &IdgamesTab::deleteAfterExtractChanged );
	connect( parallelChk_, &QCheckBox::toggled, this, [ this ]( bool enabled )
	{
		setParallelDownload( enabled );
		emit parallelDownloadChanged( enabled );
	});
	connect( unpackChk_, &QCheckBox::toggled, this, [ this ]( bool enabled )
	{
		deleteAfterExtractChk_->setEnabled( enabled );
	});

	// save the target directory as soon as the user changes it
	connect( targetDirLine_, &QLineEdit::textChanged, this, [ this ]( const QString & text )
	{
		emit targetDirChanged( text.trimmed() );
	});

	// clicking the checkbox-column header toggles all checkboxes
	connect( resultsTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, [ this ]( int logicalIndex )
	{
		if (logicalIndex == 0)
		{
			// clicking the checkbox-column header toggles all checkboxes
			bool anyChecked = false;
			for (int row = 0; row < resultsTable_->rowCount(); ++row)
			{
				if (resultsTable_->item( row, 0 ) && resultsTable_->item( row, 0 )->checkState() == Qt::Checked)
					anyChecked = true;
			}
			const Qt::CheckState newState = anyChecked ? Qt::Unchecked : Qt::Checked;
			for (int row = 0; row < resultsTable_->rowCount(); ++row)
				if (resultsTable_->item( row, 0 ))
					resultsTable_->item( row, 0 )->setCheckState( newState );
			updateDownloadBtnState();
			return;
		}

		if (logicalIndex <= 0 || logicalIndex >= resultsTable_->columnCount())
			return;

		// left-click a column header to sort ascending; clicking again inverts it
		if (sortColumn_ == logicalIndex)
			sortOrder_ = sortOrder_ == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
		else
		{
			sortColumn_ = logicalIndex;
			sortOrder_ = Qt::AscendingOrder;
		}

		// remember which entries are checked so sorting doesn't lose the selection
		QSet< int > checkedIds;
		for (int row = 0; row < results_.size(); ++row)
		{
			QTableWidgetItem * item = resultsTable_->item( row, 0 );
			if (item && item->checkState() == Qt::Checked)
				checkedIds.insert( results_[ row ].id );
		}

		const auto cmp = [ = ]( const RemoteWadEntry & a, const RemoteWadEntry & b ) -> bool
		{
			switch (sortColumn_)
			{
				case 1:  return a.title.compare( b.title, Qt::CaseInsensitive ) < 0;
				case 2:  return a.author.compare( b.author, Qt::CaseInsensitive ) < 0;
				case 3:  return a.rating < b.rating;
				case 4:  return a.votes < b.votes;
				case 5:  return a.size < b.size;
				case 6:  return a.age < b.age;   // chronologically (age = upload timestamp)
				case 7:  return a.age < b.age;   // age column
				default: return false;
			}
		};

		std::stable_sort( results_.begin(), results_.end(),
			[ & ]( const RemoteWadEntry & a, const RemoteWadEntry & b )
			{
				const bool less = cmp( a, b );
				if (less == cmp( b, a ))   // equal keys -> keep the original relative order
					return false;
				return sortOrder_ == Qt::AscendingOrder ? less : !less;
			} );

		populateResults( results_ );

		// restore the checked entries in their new positions
		for (int row = 0; row < results_.size(); ++row)
			if (checkedIds.contains( results_[ row ].id ))
				resultsTable_->item( row, 0 )->setCheckState( Qt::Checked );

		resultsTable_->horizontalHeader()->setSortIndicator( sortColumn_, sortOrder_ );
		resultsTable_->horizontalHeader()->setSortIndicatorShown( true );
		updateDownloadBtnState();
	});
}

//----------------------------------------------------------------------------------------------------------------------

void IdgamesTab::search()
{
	QString query = searchLine_->text().trimmed();

	// '*' is the user-facing wildcard, the API uses '%' as its SQL LIKE wildcard
	query.replace( '*', '%' );

	if (query.size() < 3)
	{
		setStatus( "Please enter at least 3 characters to search (use * as a wildcard)." );
		return;
	}

	startSearch( query );
}

void IdgamesTab::showAll()
{
	// '%' matches any characters, so this query matches every entry
	startSearch( QStringLiteral("%%%") );
}

void IdgamesTab::startSearch( const QString & query )
{
	// if a previous search is still running, cancel it
	if (searchReply_)
		searchReply_->abort();

	resultsTable_->setRowCount( 0 );
	results_.clear();
	detailsView_->clear();
	downloadBtn_->setEnabled( false );

	QUrl url( kApiBaseUrl );
	QUrlQuery urlQuery;
	urlQuery.addQueryItem( "action", "search" );
	urlQuery.addQueryItem( "query", query );
	urlQuery.addQueryItem( "type", typeCmb_->currentData().toString() );
	urlQuery.addQueryItem( "sort", sortCmb_->currentData().toString() );
	urlQuery.addQueryItem( "dir", dirCmb_->currentData().toString() );
	urlQuery.addQueryItem( "out", "json" );
	url.setQuery( urlQuery );

	QNetworkRequest request( url );
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	setStatus( "Searching..." );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, 0 );

	searchReply_ = network_->get( request );
	connect( searchReply_, &QNetworkReply::finished, this, &IdgamesTab::onSearchFinished );
}

void IdgamesTab::onSearchFinished()
{
	QNetworkReply * reply = searchReply_;
	searchReply_ = nullptr;

	progressBar_->setVisible( false );

	if (reply->error() != QNetworkReply::NoError)
	{
		setStatus( "Search failed: " + reply->errorString() );
		reply->deleteLater();
		return;
	}

	const QByteArray data = reply->readAll();
	reply->deleteLater();

	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson( data, &parseError );
	if (parseError.error != QJsonParseError::NoError || !doc.isObject())
	{
		setStatus( "Failed to parse the API response: " + parseError.errorString() );
		return;
	}

	const QJsonObject root = doc.object();

	if (root.contains( "error" ))
	{
		const QJsonObject err = root[ "error" ].toObject();
		setStatus( "API error: " + err[ "message" ].toString() );
		return;
	}

	QString warning;
	if (root.contains( "warning" ))
	{
		const QJsonObject warn = root[ "warning" ].toObject();
		warning = warn[ "message" ].toString();
	}

	const QJsonObject content = root[ "content" ].toObject();
	const QJsonArray files = content[ "file" ].toArray();

	QList< RemoteWadEntry > entries;
	entries.reserve( files.size() );
	for (const QJsonValue & value : files)
	{
		const QJsonObject file = value.toObject();

		RemoteWadEntry entry;
		entry.id          = file[ "id" ].toInt();
		entry.title       = file[ "title" ].toString();
		entry.author      = file[ "author" ].toString();
		entry.email       = file[ "email" ].toString();
		entry.description = file[ "description" ].toString();
		entry.rating      = file[ "rating" ].toDouble();
		entry.votes       = file[ "votes" ].toInt();
		entry.size        = qint64( file[ "size" ].toDouble() );
		entry.age         = qint64( file[ "age" ].toDouble() );
		entry.date        = file[ "date" ].toString();
		entry.dir         = file[ "dir" ].toString();
		entry.filename    = file[ "filename" ].toString();
		entries.append( std::move( entry ) );
	}

	populateResults( entries );

	if (entries.isEmpty())
	{
		setStatus( warning.isEmpty() ? "No results found." : warning );
		logMessage( warning.isEmpty() ? "No results found." : warning );
	}
	else
	{
		setStatus( QString( "Found %1 result(s)." ).arg( entries.size() ) );
		logMessage( QString( "Found %1 result(s)." ).arg( entries.size() ) );
	}
}

//----------------------------------------------------------------------------------------------------------------------

void IdgamesTab::populateResults( const QList< RemoteWadEntry > & entries )
{
	results_ = entries;

	resultsTable_->setRowCount( entries.size() );
	for (int row = 0; row < entries.size(); ++row)
	{
		const RemoteWadEntry & entry = entries[ row ];

		auto * checkItem = new QTableWidgetItem();
		checkItem->setFlags( Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable );
		checkItem->setCheckState( Qt::Unchecked );
		resultsTable_->setItem( row, 0, checkItem );

		auto * titleItem = new QTableWidgetItem( entry.title );
		titleItem->setData( Qt::UserRole, entry.id );
		resultsTable_->setItem( row, 1, titleItem );

		resultsTable_->setItem( row, 2, new QTableWidgetItem( entry.author ) );

		// rating and votes are separate columns; a WAD with no votes has no rating
		resultsTable_->setItem( row, 3, new QTableWidgetItem( entry.votes > 0 ? QString::number( entry.rating, 'f', 2 ) : "—" ) );
		resultsTable_->setItem( row, 4, new QTableWidgetItem( entry.votes > 0 ? QString::number( entry.votes ) : "—" ) );

		resultsTable_->setItem( row, 5, new QTableWidgetItem( formatSize( entry.size ) ) );
		resultsTable_->setItem( row, 6, new QTableWidgetItem( entry.date ) );

		const QString ageText = formatAge( entry.age );
		resultsTable_->setItem( row, 7, new QTableWidgetItem( ageText.isEmpty() ? "—" : ageText ) );
	}

	resultsTable_->resizeColumnsToContents();
	resultsTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	resultsTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );

	updateDownloadBtnState();
}

void IdgamesTab::onCurrentRowChanged( int currentRow, int /*currentColumn*/, int /*previousRow*/, int /*previousColumn*/ )
{
	if (currentRow < 0 || currentRow >= results_.size())
	{
		detailsView_->clear();
		return;
	}

	showDetails( currentRow );
}

void IdgamesTab::onItemChanged( QTableWidgetItem * item )
{
	if (item && item->column() == 0)
		updateDownloadBtnState();
}

void IdgamesTab::showDetails( int row )
{
	const RemoteWadEntry & entry = results_[ row ];

	QString text;
	text += "Title: " + entry.title + '\n';
	text += "Author: " + entry.author + '\n';
	if (!entry.date.isEmpty())
		text += "Date: " + entry.date + '\n';
	const QString ageText = formatAge( entry.age );
	if (!ageText.isEmpty())
		text += "Age: " + ageText + '\n';
	if (entry.votes > 0)
		text += QString( "Rating: %1 (%2 votes)\n" ).arg( entry.rating, 0, 'f', 2 ).arg( entry.votes );
	text += "Size: " + formatSize( entry.size ) + '\n';
	text += "File: " + entry.filename + '\n';
	text += '\n';
	text += entry.description;

	detailsView_->setPlainText( text );
}

//----------------------------------------------------------------------------------------------------------------------

void IdgamesTab::browseTargetDir()
{
	const QString dir = QFileDialog::getExistingDirectory( this, "Select download directory", targetDir() );
	if (dir.isEmpty())  // user cancelled
		return;

	targetDirLine_->setText( dir );  // the textChanged connection emits targetDirChanged
}

QList< int > IdgamesTab::checkedRows() const
{
	QList< int > rows;
	for (int row = 0; row < resultsTable_->rowCount(); ++row)
	{
		QTableWidgetItem * item = resultsTable_->item( row, 0 );
		if (item && item->checkState() == Qt::Checked)
			rows.append( row );
	}
	return rows;
}

void IdgamesTab::updateDownloadBtnState()
{
	downloadBtn_->setEnabled( !batchActive_ && !checkedRows().isEmpty() );
}

void IdgamesTab::downloadChecked()
{
	const QList< int > rows = checkedRows();
	if (rows.isEmpty())
	{
		setStatus( "Check the entries you want to download." );
		return;
	}

	if (!ensureTargetDirUsable())
		return;  // a message has been shown, and possibly the target dir was switched to the map dir

	// re-read the target: ensureTargetDirUsable() may have switched it to the map directory
	const QString dir = targetDir();

	// build the list of files to download, checking which already exist
	QList< PendingDownload > allDownloads;
	QStringList existingNames;
	for (int row : rows)
	{
		const RemoteWadEntry & entry = results_[ row ];
		const QString fileName = sanitizeFileName( entry.filename );

		QString filePath;
		if (autosortChk_->isChecked())
			filePath = QDir( dir ).filePath( QStringLiteral("IdGames") + '/' + fileName );  // autosort into an "IdGames" subfolder
		else
			filePath = QDir( dir ).filePath( fileName );

		if (QFile::exists( filePath ))
			existingNames.append( fileName );

		allDownloads.append( PendingDownload{ filePath, entry.relativePath() } );
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
		logMessage( "Nothing to download - all selected files already exist." );
		return;
	}

	logMessage( QString( "Starting download of %1 file(s) to \"%2\"%3." )
		.arg( pendingDownloads_.size() )
		.arg( targetDir() )
		.arg( autosortChk_->isChecked() ? " (IdGames subfolder)" : "" ) );

	batchActive_ = true;
	downloadCount_ = pendingDownloads_.size();
	downloadBtn_->setEnabled( false );

	startNextDownload();
}

void IdgamesTab::startNextDownload()
{
	// fill all free download slots up to the allowed parallelism
	while (int( activeDownloads_.size() ) < maxParallel_ && !pendingDownloads_.isEmpty())
	{
		const PendingDownload next = pendingDownloads_.takeFirst();

		auto download = std::make_unique< ActiveDownload >();
		download->path = next.filePath;
		for (const char * base : kDownloadMirrors)
			download->urls.append( QString::fromLatin1( base ) + next.relativePath );

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

void IdgamesTab::startDownload( ActiveDownload * download )
{
	if (download->urlIdx >= download->urls.size())
		return;

	const QString url = download->urls[ download->urlIdx ];

	QNetworkRequest request{ QUrl( url ) };
	request.setRawHeader( "User-Agent", kUserAgent );
	request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy );

	logMessage( QString( "Downloading \"%1\" (mirror %2/%3) via %4" )
		.arg( QFileInfo( download->path ).fileName() )
		.arg( download->urlIdx + 1 )
		.arg( download->urls.size() )
		.arg( url ) );

	// make sure the parent directory exists (autosort uses an "IdGames" subfolder)
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
}

void IdgamesTab::onDownloadFinished( ActiveDownload * download )
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

	if (success)
	{
		if (download->file)
		{
			download->file->deleteLater();
			download->file = nullptr;
		}

		// sanity check: a successful download must not be empty
		if (QFileInfo( savedPath ).size() <= 0)
		{
			success = false;
			errorString = "downloaded file is empty";
		}
	}

	if (success)
	{
		logMessage( "Downloaded to \"" + savedPath + "\"." );

		if (unpackChk_->isChecked())
		{
			// Unpack in a background worker thread so that the UI stays responsive.
			const QFileInfo savedInfo( savedPath );
			download->extractDir = savedInfo.dir().filePath( savedInfo.completeBaseName() );

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

			logMessage( "Unpacking \"" + savedPath + "\" into \"" + extractDir + "\" ..." );
			updateDownloadProgress();
			return;  // the download slot stays occupied until the unpacking finishes
		}

		emit downloadFinished( savedPath );
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
			logMessage( "Mirror failed for \"" + savedPath + "\" (" + errorString + "); trying the next mirror." );
			startDownload( download );
			return;
		}

		setStatus( "Download failed: " + errorString );
		logMessage( "Failed to download \"" + savedPath + "\": " + errorString );
	}

	updateDownloadProgress();
	removeDownload( download );
	startNextDownload();
}

void IdgamesTab::onUnpackFinished( ActiveDownload * download )
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
		logMessage( "Unpacked \"" + savedPath + "\" into \"" + extractDir + "\"." );
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

void IdgamesTab::removeDownload( ActiveDownload * download )
{
	for (size_t i = 0; i < activeDownloads_.size(); ++i)
		if (activeDownloads_[ i ].get() == download)
		{
			activeDownloads_.erase( activeDownloads_.begin() + qsizetype( i ) );
			return;
		}
}

void IdgamesTab::updateDownloadProgress()
{
	if (downloadCount_ <= 0)
		return;

	const int completed = downloadCount_ - pendingDownloads_.size() - int( activeDownloads_.size() );
	progressBar_->setVisible( true );
	progressBar_->setRange( 0, downloadCount_ );
	progressBar_->setValue( qBound( 0, completed, downloadCount_ ) );
}

//----------------------------------------------------------------------------------------------------------------------

QString IdgamesTab::sanitizeFileName( const QString & fileName ) const
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

bool IdgamesTab::ensureTargetDirUsable()
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
			return false;
		}
	}

	// it must also be writable for the downloaded files to be saved into it
	if (!fs::isDirectoryWritable( dir ))
	{
		QMessageBox::warning( this, "Target directory not usable",
			"Target directory \"" + dir + "\" is not accessible for writing.\nPlease choose another directory." );
		return false;
	}

	return true;
}

bool IdgamesTab::askToUseMapDir( const QString & targetDir )
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
			return true;
		}
	}

	return false;
}

void IdgamesTab::setStatus( const QString & text )
{
	statusLabel_->setText( text );
}

void IdgamesTab::logMessage( const QString & message )
{
	emit activityLogged( message );
}

void IdgamesTab::onHeaderContextMenu( const QPoint & pos )
{
	QMenu menu( this );

	// column 0 (the check-mark) is needed for downloads and cannot be hidden
	for (int col = 1; col < resultsTable_->columnCount(); ++col)
	{
		const QString label = resultsTable_->horizontalHeaderItem( col ) ? resultsTable_->horizontalHeaderItem( col )->text() : QString::number( col );
		QAction * action = menu.addAction( label );
		action->setCheckable( true );
		action->setChecked( !resultsTable_->isColumnHidden( col ) );
		connect( action, &QAction::triggered, this, [ this, col ]( bool checked )
		{
			resultsTable_->setColumnHidden( col, !checked );

			const QString label = resultsTable_->horizontalHeaderItem( col ) ? resultsTable_->horizontalHeaderItem( col )->text() : QString();
			if (checked)
				hiddenColumns_.removeAll( label );
			else if (!hiddenColumns_.contains( label ))
				hiddenColumns_.append( label );

			// keep the Title column stretched and the rest sized to content
			resultsTable_->horizontalHeader()->setSectionResizeMode( col, col == 1 ? QHeaderView::Stretch : QHeaderView::ResizeToContents );
			resultsTable_->resizeColumnsToContents();
			resultsTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );

			emit columnVisibilityChanged();
		});
	}

	menu.exec( resultsTable_->horizontalHeader()->mapToGlobal( pos ) );
}

void IdgamesTab::applyVisibleColumns()
{
	for (int col = 0; col < resultsTable_->columnCount(); ++col)
	{
		const QString label = resultsTable_->horizontalHeaderItem( col ) ? resultsTable_->horizontalHeaderItem( col )->text() : QString();
		resultsTable_->setColumnHidden( col, col != 0 && hiddenColumns_.contains( label ) );
	}
}

void IdgamesTab::setHiddenColumns( const QStringList & cols )
{
	hiddenColumns_ = cols;
	applyVisibleColumns();
}

QStringList IdgamesTab::hiddenColumns() const { return hiddenColumns_; }
