//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      (cacowards browser feature)
// Description: tab showing an expandable, checkable list of Cacowards-awarded WADs, downloadable from /idgames
//======================================================================================================================

#ifndef CACOWARDS_TAB_INCLUDED
#define CACOWARDS_TAB_INCLUDED


#include "Essential.hpp"

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QList>


class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;


/// One Cacowards-awarded WAD, ready to be downloaded from /idgames.
struct CacowardEntry
{
	int year = 0;
	QString category;
	QString title;
	int id = 0;          ///< /idgames file id (used only while refreshing the list)
	QString dir;         ///< path within the archive, e.g. "combos/"
	QString filename;    ///< e.g. "ww-trror.zip"

	QString relativePath() const
	{
		QString path = dir;
		if (!path.endsWith( '/' ))
			path += '/';
		return path + filename;
	}
};


//======================================================================================================================

/// Browseable tree of WADs mentioned in the Doomworld Cacowards, grouped by year and award category.
/**
  * The list is cached in a JSON file (loaded on startup) and only refreshed from the
  * network when the user clicks the "Refresh" button, so that the archive is not
  * contacted on every application run.
  */
class CacowardsTab : public QWidget {

	Q_OBJECT

 public:

	explicit CacowardsTab( QWidget * parent = nullptr );
	virtual ~CacowardsTab() override;

	void setTargetDir( const QString & dir );
	QString targetDir() const;

	/// Sets the path of the JSON file the Cacowards list is loaded from and saved to.
	void setDataFilePath( const QString & path );

 signals:

	/// Emitted after a file has been successfully downloaded and saved to disk.
	void downloadFinished( const QString & filePath );

	/// Emitted when the user changes the download target directory.
	void targetDirChanged( const QString & dir );

 private slots:

	void refresh();
	void importExportedXml();
	void onCurrentItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * previous );
	void onItemChanged( QTreeWidgetItem * item, int column );
	void browseTargetDir();
	void downloadChecked();
	void onDownloadFinished();
	void onRefreshYearFetched();
	void onRefreshIdResolved();

 private:

	/// A file queued for download.
	struct PendingDownload
	{
		QString filePath;     ///< absolute path where the file will be saved
		QString relativePath; ///< path of the file within the archive (used to build mirror URLs)
	};

	void buildUi();
	void loadData();
	bool loadDataFromJson( const QByteArray & json, QList< CacowardEntry > & out ) const;
	void buildTree();
	void setStatus( const QString & text );
	QString sanitizeFileName( const QString & fileName ) const;
	bool ensureTargetDirExists();
	QList< PendingDownload > collectCheckedDownloads() const;
	void updateDownloadBtnState();
	void startNextDownload();
	void startDownload( const QString & filePath );

	// list refresh (fetch from doomwiki, resolve ids, save JSON)
	void startNextRefreshYear();
	void parseCacowardsWikitext( int year, const QString & wikitext, QList< CacowardEntry > & out ) const;
	void parseCacowardsHtml( int year, const QString & html, QList< CacowardEntry > & out ) const;
	void parseExportXml( const QByteArray & xml, QList< CacowardEntry > & out ) const;
	void fallbackToBrowserExport();
	void startNextRefreshResolution();
	void saveDataFile();

	QNetworkAccessManager * network_;
	QNetworkReply * downloadReply_ = nullptr;
	QFile * downloadFile_ = nullptr;   ///< file currently being written to during a download
	QString downloadPath_;             ///< absolute path of the file currently being downloaded
	QStringList downloadUrls_;         ///< candidate mirror URLs for the current download
	int downloadUrlIdx_ = 0;           ///< index of the mirror currently being tried
	QList< PendingDownload > pendingDownloads_;   ///< files still waiting to be downloaded
	int downloadCount_ = 0;            ///< total number of files in the current download batch
	bool batchActive_ = false;         ///< whether a download batch is currently running
	bool updatingChecks_ = false;      ///< guard against recursive itemChanged handling

	QList< CacowardEntry > entries_;   ///< the currently displayed list

	// refresh state
	QNetworkReply * refreshReply_ = nullptr;
	QList< int > refreshYears_;        ///< years still to be fetched
	int refreshYearIdx_ = 0;
	QList< CacowardEntry > refreshEntries_;   ///< entries accumulated while refreshing
	int refreshResolveIdx_ = 0;        ///< index of the next entry whose id needs resolving
	int refreshResolvedCount_ = 0;     ///< number of successfully resolved entries

	// widgets

	QTreeWidget * tree_ = nullptr;
	QTextEdit * detailsView_ = nullptr;
	QLineEdit * targetDirLine_ = nullptr;
	QPushButton * refreshBtn_ = nullptr;
	QPushButton * importBtn_ = nullptr;
	QPushButton * browseBtn_ = nullptr;
	QPushButton * downloadBtn_ = nullptr;
	QLabel * statusLabel_ = nullptr;
	QProgressBar * progressBar_ = nullptr;
	QString dataFilePath_;

};


#endif // CACOWARDS_TAB_INCLUDED
