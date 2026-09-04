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

	/// Shows/hides the activity log. Shared across the download tabs via MainWindow.
	void setLogVisible( bool visible );

 signals:

	/// Emitted after a file has been successfully downloaded and saved to disk.
	void downloadFinished( const QString & filePath );

	/// Emitted when the user changes the download target directory.
	void targetDirChanged( const QString & dir );

	/// Emitted when the user shows/hides the activity log (propagated to the other download tabs).
	void logVisibilityChanged( bool enabled );

 private slots:

	void search();
	void showAll();
	void onSearchFinished();
	void onCurrentRowChanged( int currentRow, int currentColumn, int previousRow, int previousColumn );
	void onItemChanged( QTableWidgetItem * item );
	void browseTargetDir();
	void downloadChecked();
	void onDownloadFinished();

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

	void buildUi();
	void populateResults( const QList< RemoteWadEntry > & entries );
	void showDetails( int row );
	void setStatus( const QString & text );
	void logMessage( const QString & message );
	QString sanitizeFileName( const QString & fileName ) const;
	bool ensureTargetDirUsable();
	bool askToUseMapDir( const QString & targetDir );
	void startSearch( const QString & query );
	QList< int > checkedRows() const;
	void updateDownloadBtnState();
	void startDownload( const QString & filePath );
	void startNextDownload();

	QNetworkAccessManager * network_;
	QNetworkReply * searchReply_ = nullptr;
	QNetworkReply * downloadReply_ = nullptr;
	QFile * downloadFile_ = nullptr;   ///< file currently being written to during a download
	QString downloadPath_;             ///< absolute path of the file currently being downloaded
	QStringList downloadUrls_;         ///< candidate mirror URLs for the current download
	int downloadUrlIdx_ = 0;           ///< index of the mirror currently being tried
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
	QCheckBox * logChk_ = nullptr;          ///< toggles visibility of the activity log
	QPlainTextEdit * logView_ = nullptr;    ///< scrollable activity log (hidden by default)
	bool settingLogVisible_ = false;       ///< guards against re-emitting logVisibilityChanged while setting programmatically

};


#endif // IDGAMES_TAB_INCLUDED
