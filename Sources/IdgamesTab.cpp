//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      (idgames browser feature)
// Description: tab for searching and downloading WADs/ZIPs from the Doomworld /idgames archive
//======================================================================================================================

#include "IdgamesTab.hpp"

#include "Utils/FileSystemUtils.hpp"  // isDirectoryWritable

#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>
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
	if (downloadReply_)
		downloadReply_->abort();
	delete downloadFile_;
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
	resultsTable_->setColumnCount( 6 );
	resultsTable_->setHorizontalHeaderLabels( QStringList{ "✓", "Title", "Author", "Rating", "Size", "Date" } );
	resultsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
	resultsTable_->setSelectionMode( QAbstractItemView::SingleSelection );
	resultsTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
	resultsTable_->verticalHeader()->setVisible( false );
	resultsTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	resultsTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );

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

	// save the target directory as soon as the user changes it
	connect( targetDirLine_, &QLineEdit::textChanged, this, [ this ]( const QString & text )
	{
		emit targetDirChanged( text.trimmed() );
	});

	// clicking the checkbox-column header toggles all checkboxes
	connect( resultsTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, [ this ]( int logicalIndex )
	{
		if (logicalIndex != 0)
			return;

		bool anyChecked = false;
		for (int row = 0; row < resultsTable_->rowCount(); ++row)
		{
			if (resultsTable_->item( row, 0 ) && resultsTable_->item( row, 0 )->checkState() == Qt::Checked)
				anyChecked = true;
		}

		const Qt::CheckState newState = anyChecked ? Qt::Unchecked : Qt::Checked;
		for (int row = 0; row < resultsTable_->rowCount(); ++row)
		{
			if (resultsTable_->item( row, 0 ))
				resultsTable_->item( row, 0 )->setCheckState( newState );
		}

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
		entry.date        = file[ "date" ].toString();
		entry.dir         = file[ "dir" ].toString();
		entry.filename    = file[ "filename" ].toString();
		entries.append( std::move( entry ) );
	}

	populateResults( entries );

	if (entries.isEmpty())
		setStatus( warning.isEmpty() ? "No results found." : warning );
	else
		setStatus( QString( "Found %1 result(s)." ).arg( entries.size() ) );
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

		QString ratingText = "—";
		if (entry.votes > 0)
			ratingText = QString( "%1 (%2)" ).arg( entry.rating, 0, 'f', 2 ).arg( entry.votes );
		resultsTable_->setItem( row, 3, new QTableWidgetItem( ratingText ) );

		resultsTable_->setItem( row, 4, new QTableWidgetItem( formatSize( entry.size ) ) );
		resultsTable_->setItem( row, 5, new QTableWidgetItem( entry.date ) );
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

	const QString dir = targetDir();
	if (dir.isEmpty())
	{
		setStatus( "Choose a target directory first." );
		return;
	}

	if (!ensureTargetDirUsable())
		return;  // a message has been shown, and possibly the target dir was switched to the map dir

	// build the list of files to download, checking which already exist
	QList< PendingDownload > allDownloads;
	QStringList existingNames;
	for (int row : rows)
	{
		const RemoteWadEntry & entry = results_[ row ];
		const QString fileName = sanitizeFileName( entry.filename );
		const QString filePath = QDir( dir ).filePath( fileName );

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
			// keep only the files that don't exist yet
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

void IdgamesTab::startNextDownload()
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

void IdgamesTab::startDownload( const QString & filePath )
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

	connect( downloadReply_, &QNetworkReply::finished, this, &IdgamesTab::onDownloadFinished );

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

void IdgamesTab::onDownloadFinished()
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
	const QString dir = targetDir();
	if (dir.isEmpty())
		return false;

	// create it if it doesn't exist yet
	if (!QFileInfo::exists( dir ))
	{
		if (!QDir().mkpath( dir ))
			return askToUseMapDir( dir );
	}

	// it must also be writable for the downloaded files to be saved into it
	if (!fs::isDirectoryWritable( dir ))
		return askToUseMapDir( dir );

	return true;
}

bool IdgamesTab::askToUseMapDir( const QString & targetDir )
{
	if (!mapSourceDir_.isEmpty() && QFileInfo::exists( mapSourceDir_ ) && fs::isDirectoryWritable( mapSourceDir_ ))
	{
		const auto answer = QMessageBox::question(
			this, "Target directory not usable",
			"Target directory \"" + targetDir + "\" is not accessible for writing.\n\n"
			"Use your map directory \"" + mapSourceDir_ + "\" as the download target instead?",
			QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes
		);
		if (answer == QMessageBox::Yes)
		{
			targetDirLine_->setText( mapSourceDir_ );  // also re-saves the target dir preference
			return true;
		}
		return false;
	}

	QMessageBox::warning( this, "Target directory not usable",
		"Target directory \"" + targetDir + "\" is not accessible for writing.\n"
		"Please choose another directory." );
	return false;
}

void IdgamesTab::setStatus( const QString & text )
{
	statusLabel_->setText( text );
}
