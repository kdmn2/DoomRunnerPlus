//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      (idgames browser feature)
// Description: tab for searching and downloading WADs/ZIPs from the Doomworld /idgames archive
//======================================================================================================================

#ifndef IDGAMES_TAB_INCLUDED
#define IDGAMES_TAB_INCLUDED


#include "Essential.hpp"

#include <QWidget>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

template< typename T > class QFutureWatcher;


class QLineEdit;
class QPushButton;
class QComboBox;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;
class QPlainTextEdit;
class QCheckBox;
class QLabel;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;


//======================================================================================================================

/// Browser for the Doomworld /idgames archive.
/**
  * Searches the archive through its public HTTP API, displays the results in a table
  * and allows downloading one or more checked entries into a user-chosen directory.
  * When a download completes, a signal is emitted so that the main window can add
  * the downloaded file into the currently selected preset's mod list.
  */
class IdgamesTab : public QWidget {

	Q_OBJECT

 public:

	explicit IdgamesTab( QWidget * parent = nullptr );
	virtual ~IdgamesTab() override;

	void setTargetDir( const QString & dir );
	QString targetDir() const;

	/// Sets the directory used as the source of map packs (used as a fallback download target suggestion).
	void setMapSourceDir( const QString & dir );

	void setAutosort( bool enabled );
	bool autosort() const;
	void setUnpack( bool enabled );
	bool unpack() const;
	void setDeleteAfterExtract( bool enabled );
	bool deleteAfterExtract() const;
	void setParallelDownload( bool enabled );
	bool parallelDownload() const;

	/// Sets which result columns are hidden (by header label) and applies it to the table.
	void setHiddenColumns( const QStringList & cols );

	/// Returns the currently hidden result columns (by header label).
	QStringList hiddenColumns() const;

 signals:

	/// Emitted after a file has been successfully downloaded and saved to disk.
	void downloadFinished( const QString & filePath );

	/// Emitted when the user changes the download target directory.
	void targetDirChanged( const QString & dir );

	/// Emitted when the user shows/hides a results column (so it can be persisted).
	void columnVisibilityChanged();

	/// Emitted for each activity-log message (collected in the single shared WAD Downloader log window).
	void activityLogged( const QString & message );

	/// Emitted when the user changes one of the download options.
	void autosortChanged( bool enabled );
	void unpackChanged( bool enabled );
	void deleteAfterExtractChanged( bool enabled );
	void parallelDownloadChanged( bool enabled );

 private slots:

	void search();
	void showAll();
	void onSearchFinished();
	void onCurrentRowChanged( int currentRow, int currentColumn, int previousRow, int previousColumn );
	void onItemChanged( QTableWidgetItem * item );
	void onHeaderContextMenu( const QPoint & pos );
	void browseTargetDir();
	void downloadChecked();

 private:

	/// One entry returned by the /idgames search API.
	struct RemoteWadEntry
	{
		int id = 0;
		QString title;
		QString author;
		QString email;
		QString description;
		double rating = 0.0;
		int votes = 0;
		qint64 size = 0;       ///< size in bytes
		qint64 age = 0;        ///< upload time as a Unix timestamp (seconds since epoch)
		QString date;          ///< upload date as YYYY-MM-DD
		QString dir;           ///< path within the archive, e.g. "combos/"
		QString filename;      ///< e.g. "ww-trror.zip"

		/// Path of the file relative to the root of any /idgames mirror, e.g. "combos/ww-trror.zip".
		QString relativePath() const
		{
			QString path = dir;
			if (!path.endsWith( '/' ))
				path += '/';
			return path + filename;
		}
	};

	/// A file queued for download.
	struct PendingDownload
	{
		QString filePath;     ///< absolute path where the file will be saved
		QString relativePath; ///< path of the file within the archive (used to build mirror URLs)
	};

	/// A download that is currently in progress (one per active network request).
	struct ActiveDownload
	{
		QNetworkReply * reply = nullptr;
		QFile * file = nullptr;                 ///< file currently being written to
		QString path;                           ///< absolute path of the file being downloaded
		QStringList urls;                       ///< candidate mirror URLs for this file
		int urlIdx = 0;                         ///< index of the mirror currently being tried
		QFutureWatcher< bool > * unpackWatcher = nullptr;   ///< watcher for the background unpacking, null if none
		QString extractDir;                     ///< directory the archive is being extracted into (while unpacking)
	};

	void buildUi();
	void populateResults( const QList< RemoteWadEntry > & entries );
	void showDetails( int row );
	void setStatus( const QString & text );
	void logMessage( const QString & message );
	void applyVisibleColumns();
	QString sanitizeFileName( const QString & fileName ) const;
	bool ensureTargetDirUsable();
	bool askToUseMapDir( const QString & targetDir );
	void startSearch( const QString & query );
	QList< int > checkedRows() const;
	void updateDownloadBtnState();
	void startDownload( ActiveDownload * download );
	void startNextDownload();
	void onDownloadFinished( ActiveDownload * download );
	void onUnpackFinished( ActiveDownload * download );
	void removeDownload( ActiveDownload * download );
	void updateDownloadProgress();

	QNetworkAccessManager * network_;
	QNetworkReply * searchReply_ = nullptr;
	std::vector< std::unique_ptr< ActiveDownload > > activeDownloads_;   ///< downloads in progress (up to maxParallel_)
	QList< PendingDownload > pendingDownloads_;   ///< files still waiting to be downloaded
	int downloadCount_ = 0;            ///< total number of files in the current download batch
	bool batchActive_ = false;         ///< whether a download batch is currently running

	QList< RemoteWadEntry > results_;

	QString mapSourceDir_;   ///< directory used as the source of map packs (suggested as a fallback download target)

	// widgets

	QLineEdit * searchLine_ = nullptr;
	QPushButton * searchBtn_ = nullptr;
	QPushButton * showAllBtn_ = nullptr;
	QComboBox * typeCmb_ = nullptr;
	QComboBox * sortCmb_ = nullptr;
	QComboBox * dirCmb_ = nullptr;
	QTableWidget * resultsTable_ = nullptr;
	QTextEdit * detailsView_ = nullptr;
	QLineEdit * targetDirLine_ = nullptr;
	QPushButton * browseBtn_ = nullptr;
	QPushButton * downloadBtn_ = nullptr;
	QLabel * statusLabel_ = nullptr;
	QProgressBar * progressBar_ = nullptr;
	QCheckBox * autosortChk_ = nullptr;
	QCheckBox * unpackChk_ = nullptr;
	QCheckBox * deleteAfterExtractChk_ = nullptr;
	QCheckBox * parallelChk_ = nullptr;
	int maxParallel_ = 1;                   ///< how many files are downloaded at the same time
	QStringList hiddenColumns_;             ///< result columns hidden by the user (by header label)
	int sortColumn_ = -1;                   ///< results column clicked to sort ( -1 = no sort, API order )
	Qt::SortOrder sortOrder_ = Qt::AscendingOrder;

};


#endif // IDGAMES_TAB_INCLUDED
