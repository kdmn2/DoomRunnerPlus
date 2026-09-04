//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Description: tab that lists WADs from various Doomworld "top lists" (Top 100 WADs of All Time,
//              Top 100 Most Memorable Maps, Top 25 Missed Cacowards, Missed Cacowards 2), grouped by
//              source and (optionally) year, and lets the user download them from /idgames.
//======================================================================================================================

#ifndef TOPWADS_TAB_INCLUDED
#define TOPWADS_TAB_INCLUDED


#include "Essential.hpp"

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <vector>


class QTreeWidget;
class QTreeWidgetItem;
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


/// One WAD from one of the supported "top list" pages.
struct TopWadEntry
{
	QString umbrella;    ///< top-level tree node (source name), e.g. "Top 100 WADs of All Time"
	QString subgroup;    ///< sub-group: a year ("1994"), "Part 1"/"Part 2", or empty (flat)
	QString title;
	int id = 0;          ///< /idgames id (0 = no id; path is given directly or unavailable)
	QString dir;         ///< path within the archive for file= references
	QString filename;    ///< file name (may be empty when only an id is known)

	QString relativePath() const
	{
		QString path = dir;
		if (!path.isEmpty() && !path.endsWith( '/' ))
			path += '/';
		return path + filename;
	}
};

/// Describes a single source page and how its entries are grouped in the tree.
struct TopListSource
{
	QString umbrella;       ///< top-level tree node (source name)
	QString pageTitle;      ///< doomwiki page title (for the Wayback refresh / export)
	enum class Grouping { Year, Flat, Part } grouping = Grouping::Year;
	QString part;           ///< for Grouping::Part, e.g. "Part 1"
};

/// The set of pages shown by this tab.
const TopListSource & getTopListSource( int idx );
int topListSourceCount();


//======================================================================================================================

/// Tab that shows and downloads WADs from the Doomworld "top lists".
class TopWadsTab : public QWidget {

	Q_OBJECT

	struct ActiveDownload;  // forward declaration (defined below in the private section)

 public:

	explicit TopWadsTab( QWidget * parent = nullptr );
	virtual ~TopWadsTab() override;

	void setTargetDir( const QString & dir );
	QString targetDir() const;

	void setAutosort( bool enabled );
	bool autosort() const;
	void setUnpack( bool enabled );
	bool unpack() const;
	void setDeleteAfterExtract( bool enabled );
	bool deleteAfterExtract() const;

	void setDataFilePath( const QString & path );

	QStringList expandedNodes() const;
	void setExpandedNodes( const QStringList & nodes );

	void setParallelDownload( bool enabled );
	bool parallelDownload() const;

	void setMapSourceDir( const QString & dir );

	/// Shows/hides the activity log. Shared across the download tabs via MainWindow.
	void setLogVisible( bool visible );

 signals:

	void downloadFinished( const QString & filePath );
	void targetDirChanged( const QString & dir );
	void autosortChanged( bool enabled );
	void unpackChanged( bool enabled );
	void deleteAfterExtractChanged( bool enabled );
	void expansionChanged();
	void parallelDownloadChanged( bool enabled );

	/// Emitted when the user shows/hides the activity log (propagated to the other download tabs).
	void logVisibilityChanged( bool enabled );

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

 private:

	/// A file queued for download.
	struct PendingDownload
	{
		QString filePath;
		QString relativePath;
		QString title;
		int id = 0;
		QString umbrella;
		QString subgroup;
	};

	struct ActiveDownload
	{
		QNetworkReply * reply = nullptr;
		QNetworkReply * resolveReply = nullptr;
		QFile * file = nullptr;
		QString path;
		QString title;
		QStringList urls;
		int urlIdx = 0;
		int resolveId = 0;
		QString umbrella;
		QString subgroup;
		QFutureWatcher< bool > * unpackWatcher = nullptr;
		QString extractDir;
	};

	void buildUi();
	void loadData();
	void parseExportXml( const QByteArray & xml );
	void parseSourceWikitext( const TopListSource & src, const QString & wikitext );
	void startNextRefresh();
	void buildTree( bool restoreExpansion = false );
	void applyExpansionState();
	void openBrowserExportPage();
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
	void saveDataFile();

	QNetworkAccessManager * network_;
	QList< PendingDownload > pendingDownloads_;
	int downloadCount_ = 0;
	bool batchActive_ = false;
	std::vector< std::unique_ptr< ActiveDownload > > activeDownloads_;
	int maxParallel_ = 1;

	QList< TopWadEntry > entries_;

	QStringList expandedNodes_;
	bool restoringExpansion_ = false;

	// refresh state (used for the Wayback-based refresh)
	QNetworkReply * refreshReply_ = nullptr;
	int refreshSourceIdx_ = 0;
	QByteArray refreshData_;

	// widgets
	QTreeWidget * tree_ = nullptr;
	QPlainTextEdit * detailsView_ = nullptr;
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
	QCheckBox * logChk_ = nullptr;
	QLabel * statusLabel_ = nullptr;
	QProgressBar * progressBar_ = nullptr;
	QPlainTextEdit * logView_ = nullptr;
	QString dataFilePath_;
	QString mapSourceDir_;
	bool settingLogVisible_ = false;   ///< guards against re-emitting logVisibilityChanged while setting programmatically

};


#endif // TOPWADS_TAB_INCLUDED
