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
#include <QElapsedTimer>
#include <memory>  // std::unique_ptr
#include <vector>


class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QProgressBar;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;
template< typename T > class QFutureWatcher;


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

	struct ActiveDownload;  // forward declaration (defined below in the private section)

 public:

	explicit CacowardsTab( QWidget * parent = nullptr );
	virtual ~CacowardsTab() override;

	void setTargetDir( const QString & dir );
	QString targetDir() const;

	void setAutosort( bool enabled );
	bool autosort() const;
	void setUnpack( bool enabled );
	bool unpack() const;
	void setDeleteAfterExtract( bool enabled );
	bool deleteAfterExtract() const;

	/// Sets the path of the JSON file the Cacowards list is loaded from and saved to.
	void setDataFilePath( const QString & path );

	/// Returns the list of currently expanded nodes (each as a path like "2004"/"2004/Winners").
	QStringList expandedNodes() const;

	/// Restores the expansion state saved on a previous run (applied when the tree is rebuilt).
	void setExpandedNodes( const QStringList & nodes );

	void setParallelDownload( bool enabled );
	bool parallelDownload() const;

	/// Sets the directory used as the source of map packs (used as a fallback download target suggestion).
	void setMapSourceDir( const QString & dir );

 signals:

	/// Emitted after a file has been successfully downloaded and saved to disk.
	void downloadFinished( const QString & filePath );

	/// Emitted when the user changes the download target directory.
	void targetDirChanged( const QString & dir );

	/// Emitted when the user changes one of the download options.
	void autosortChanged( bool enabled );
	void unpackChanged( bool enabled );
	void deleteAfterExtractChanged( bool enabled );

	/// Emitted whenever the user expands or collapses a node in the list.
	void expansionChanged();

	/// Emitted when the user toggles parallel downloads.
	void parallelDownloadChanged( bool enabled );

 private slots:

	void refresh();
	void generateList();
	void importExportedXml();
	void onCurrentItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * previous );
	void onItemChanged( QTreeWidgetItem * item, int column );
	void onExpansionChanged();
	void browseTargetDir();
	void downloadChecked();
	void onDownloadFinished( ActiveDownload * download );
	void onRefreshYearFetched();
	void onRefreshIdResolved();

 private:

	/// A file queued for download.
	struct PendingDownload
	{
		QString filePath;     ///< absolute path where the file will be saved (empty until an id has been resolved)
		QString relativePath; ///< path of the file within the archive (used to build mirror URLs)
		QString title;        ///< entry title, used to name the folder when unpacking
		int id = 0;           ///< /idgames id, set when the download path still needs to be resolved (0 = path already known)
		int year = 0;         ///< entry year (used if the path must be built after resolving the id)
		QString category;     ///< entry category (used if the path must be built after resolving the id)
	};

	/// A download that is currently in progress (one per active network request).
	struct ActiveDownload
	{
		QNetworkReply * reply = nullptr;
		QNetworkReply * resolveReply = nullptr; ///< reply while resolving an /idgames id, null otherwise
		QFile * file = nullptr;         ///< file currently being written to
		QString path;                   ///< absolute path of the file being downloaded
		QString title;                  ///< entry title, used to name the folder when unpacking
		QStringList urls;               ///< candidate mirror URLs for this file
		int urlIdx = 0;                 ///< index of the mirror currently being tried
		int resolveId = 0;              ///< /idgames id to resolve before downloading (0 = path already known)
		int year = 0;                   ///< entry year, used when the path is built after resolving the id
		QString category;               ///< entry category, used when the path is built after resolving the id
		QFutureWatcher< bool > * unpackWatcher = nullptr;   ///< watcher for the background unpacking, null if no unpacking in progress
		QString extractDir;             ///< directory the archive is being extracted into (while unpacking)
	};

	void buildUi();
	void loadData();
	bool loadDataFromJson( const QByteArray & json, QList< CacowardEntry > & out ) const;
	void buildTree( bool restoreExpansion = false );
	void applyExpansionState();
	void setStatus( const QString & text );
	void logMessage( const QString & message );
	QString sanitizeFileName( const QString & fileName ) const;
	QList< PendingDownload > collectCheckedDownloads() const;
	bool ensureTargetDirUsable();
	bool askToUseMapDir( const QString & targetDir );
	void updateDownloadBtnState();
	void startNextDownload();
	void startDownload( ActiveDownload * download );
	void onDownloadIdResolved( ActiveDownload * download );
	void onUnpackFinished( ActiveDownload * download );
	void removeDownload( ActiveDownload * download );
	void updateDownloadProgress();

	// list refresh (fetch from doomwiki, resolve ids, save JSON)
	void startNextRefreshYear();
	void parseCacowardsWikitext( int year, const QString & wikitext, QList< CacowardEntry > & out ) const;
	void parseCacowardsHtml( int year, const QString & html, QList< CacowardEntry > & out ) const;
	void parseExportXml( const QByteArray & xml, QList< CacowardEntry > & out ) const;
	void openBrowserExportPage();
	void startNextRefreshResolution();
	void updateResolveProgress( const CacowardEntry & entry );
	static QString formatRemainingEstimate( qint64 ms );
	void saveDataFile();

	QNetworkAccessManager * network_;
	QList< PendingDownload > pendingDownloads_;   ///< files still waiting to be downloaded
	int downloadCount_ = 0;            ///< total number of files in the current download batch
	bool batchActive_ = false;         ///< whether a download batch is currently running
	std::vector< std::unique_ptr< ActiveDownload > > activeDownloads_;   ///< downloads currently in progress (up to maxParallel_)
	int maxParallel_ = 1;              ///< how many files are downloaded at the same time

	QList< CacowardEntry > entries_;   ///< the currently displayed list

	// expansion state
	QStringList expandedNodes_;        ///< saved list of expanded nodes (each as a path like "2004"/"2004/Winners")
	bool restoringExpansion_ = false;  ///< guards against emitting expansionChanged while programmatically restoring the state

	// refresh state
	QNetworkReply * refreshReply_ = nullptr;
	QList< int > refreshYears_;        ///< years still to be fetched
	int refreshYearIdx_ = 0;
	QList< CacowardEntry > refreshEntries_;   ///< entries accumulated while refreshing
	int refreshResolveIdx_ = 0;        ///< index of the next entry whose id needs resolving
	int refreshResolvedCount_ = 0;     ///< number of successfully resolved entries
	int refreshNetworkSteps_ = 0;      ///< number of idgames API requests already answered (for the ETA estimate)
	QElapsedTimer refreshTimer_;       ///< measures how long the resolution phase has been running

	// widgets

	QTreeWidget * tree_ = nullptr;
	QTextEdit * detailsView_ = nullptr;
	QLineEdit * targetDirLine_ = nullptr;
	QPushButton * refreshBtn_ = nullptr;
	QPushButton * generateListBtn_ = nullptr;
	QPushButton * importBtn_ = nullptr;
	QPushButton * browseBtn_ = nullptr;
	QPushButton * downloadBtn_ = nullptr;
	QCheckBox * autosortChk_ = nullptr;
	QCheckBox * unpackChk_ = nullptr;
	QCheckBox * deleteAfterExtractChk_ = nullptr;
	QCheckBox * parallelChk_ = nullptr;
	QCheckBox * logChk_ = nullptr;          ///< toggles visibility of the activity log
	QLabel * statusLabel_ = nullptr;
	QProgressBar * progressBar_ = nullptr;
	QPlainTextEdit * logView_ = nullptr;    ///< scrollable activity log (hidden by default)
	QString dataFilePath_;
	QString mapSourceDir_;   ///< directory used as the source of map packs (suggested as a fallback download target)

};


#endif // CACOWARDS_TAB_INCLUDED
