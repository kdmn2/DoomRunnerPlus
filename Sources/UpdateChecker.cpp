//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: asynchronous update checking tool
//======================================================================================================================

#include "UpdateChecker.hpp"

#include "AppVersion.hpp"
#include "Themes.hpp"  // updateWindowBorder
#include "Utils/Version.hpp"
#include "Utils/LangUtils.hpp"  // atScopeEndDo
#include "Utils/WidgetUtils.hpp"  // HYPERLINK

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QStringBuilder>
#include <QMessageBox>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QTextBrowser>


//======================================================================================================================
// UpdateChecker

static const QString latestReleaseApiUrl = "https://api.github.com/repos/kdmn2/DoomRunnerPlus/releases/latest";
static const QString releasePageUrl = "https://github.com/kdmn2/DoomRunnerPlus/releases";


UpdateChecker::UpdateChecker()
:
	LoggingComponent( u"UpdateChecker" )
{
	QObject::connect( &manager, &QNetworkAccessManager::finished, this, &UpdateChecker::requestFinished );
}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::checkForUpdates_async( ResultCallback && callback )
{
	QNetworkRequest request;
	request.setUrl( latestReleaseApiUrl );
	request.setRawHeader( "Accept", "application/vnd.github+json" );
	request.setRawHeader( "User-Agent", "DoomRunnerPlus" );
	QNetworkReply * reply = manager.get( request );

	pendingRequests[ reply ] = { std::move(callback) };
}

void UpdateChecker::requestFinished( QNetworkReply * reply )
{
	auto requestIter = pendingRequests.find( reply );
	if (requestIter == pendingRequests.end())
	{
		logLogicError() << "This reply does not have a registered callback, wtf?";
		return;
	}
	RequestData & requestData = requestIter.value();

	auto guard = atScopeEndDo( [&](){ pendingRequests.erase( requestIter ); } );

	if (reply->error())
	{
		//logRuntimeError() << "HTTP request failed: " << reply->errorString();  // handled in callback when appropriate
		requestData.callback( ConnectionFailed, reply->errorString(), {} );
		return;
	}

	releaseReceived( reply, requestData );
}

void UpdateChecker::releaseReceived( QNetworkReply * reply, RequestData & requestData )
{
	const QJsonDocument doc = QJsonDocument::fromJson( reply->readAll() );
	if (!doc.isObject())
	{
		requestData.callback( InvalidFormat, "invalid JSON", {} );
		return;
	}

	const QJsonObject release = doc.object();

	// the release tag names the version, e.g. "v2.0.0"
	QString versionStr = release[ "tag_name" ].toString();
	if (versionStr.startsWith( 'v' ) || versionStr.startsWith( 'V' ))
		versionStr.remove( 0, 1 );

	const Version availableVersion( versionStr );
	if (!availableVersion.isValid())
	{
		logLogicError().noquote() << "Release tag from github is in invalid format ("<<versionStr<<"). Fix it!";
		requestData.callback( InvalidFormat, std::move(versionStr), {} );
		return;
	}

	if (!(availableVersion > appVersion))
	{
		requestData.callback( UpdateNotAvailable, {}, { versionStr } );
		return;
	}

	// find the .AppImage download link among the release assets
	QString downloadUrl;
	const QJsonArray assets = release[ "assets" ].toArray();
	for (const QJsonValue & value : assets)
	{
		const QJsonObject asset = value.toObject();
		const QString name = asset[ "name" ].toString();
		if (name.endsWith( QLatin1String(".AppImage"), Qt::CaseInsensitive ))
		{
			downloadUrl = asset[ "browser_download_url" ].toString();
			break;
		}
	}

	// versionInfo contract: [0] version, [1] download URL, [2..] release notes
	QStringList versionInfo;
	versionInfo.append( versionStr );
	versionInfo.append( downloadUrl );
	versionInfo.append( release[ "body" ].toString().split( '\n' ) );

	requestData.callback( UpdateAvailable, {}, std::move(versionInfo) );
}


//======================================================================================================================
// common result reactions

struct NewElements
{
	QLabel * firstLabel = nullptr;
	QTextBrowser * textBrowser = nullptr;
	QLabel * secondLabel = nullptr;

	operator bool() const { return firstLabel && textBrowser && secondLabel; }
};
static NewElements reworkLayout( QMessageBox & msgBox )
{
	NewElements newElements;

	// We need to do all of this mess just to customize the content of the message box and add a text field.
	// Beware: This code is kinda fragile since it depends on the exact implementation of QMessageBox and its layout.

	QGridLayout * layout = qobject_cast< QGridLayout * >( msgBox.layout() );
	if (!layout)
	{
		::logLogicError( u"UpdateChecker::reworkLayout" ) << "MessageBox doesn't use grid layout, wtf?";
		return newElements;
	}

	/* the original layout looks like this
	 QIcon             QSpacerItem       QLabel
	 QIcon             QSpacerItem      (QCheckBox)
	(QSpacerItem       nullptr           nullptr)
	 QDialogButtonBox  QDialogButtonBox  QDialogButtonBox
	*/
	/* but we want it like this
	 QIcon             QSpacerItem       QLabel
	 QIcon             QSpacerItem       QTextBrowser
	 nullptr           nullptr           QLabel
	 nullptr           nullptr          (QCheckBox)
	(QSpacerItem       nullptr           nullptr)
	 QDialogButtonBox  QDialogButtonBox  QDialogButtonBox
	*/

	int origLastRow = layout->rowCount() - 1;

	// move button box 2 rows down
	QDialogButtonBox * btnBox = msgBox.findChild< QDialogButtonBox * >();
	if (!btnBox)
	{
		::logLogicError( u"UpdateChecker::reworkLayout" ) << "MessageBox doesn't have button box, wtf?";
		return newElements;
	}
	layout->removeWidget( btnBox );
	layout->addWidget( btnBox, origLastRow + 2, 0, 1, layout->columnCount() );

	// move checkbox and its related layout items 2 rows down
	QCheckBox * chkBox = msgBox.findChild< QCheckBox * >();
	if (chkBox)
	{
		int boxRow, boxColumn, boxRowSpan, boxColumnSpan;
		layout->getItemPosition( layout->indexOf( chkBox ), &boxRow, &boxColumn, &boxRowSpan, &boxColumnSpan );
		layout->removeWidget( chkBox );
		layout->addWidget( chkBox, boxRow + 2, boxColumn, boxRowSpan, boxColumnSpan, Qt::AlignLeft );

		for (int itemColumn = 0; itemColumn < layout->columnCount(); ++itemColumn)
		{
			QLayoutItem * item = layout->itemAtPosition( boxRow + 1, itemColumn );
			if (item)
			{
				layout->removeItem( item );
				layout->addItem( item, boxRow + 3, itemColumn, 1, 1 );  // we asume only 1x1 items, hopefully this is not gonna change
			}
		}
	}

	// find the original label
	newElements.firstLabel = msgBox.findChild< QLabel * >( "qt_msgbox_label" );
	if (!newElements.firstLabel)
	{
		::logLogicError( u"UpdateChecker::reworkLayout" ) << "MessageBox doesn't have this label, incorrect name?";
		return newElements;
	}
	int labelRow, labelColumn, labelRowSpan, labelColumnSpan;
	layout->getItemPosition( layout->indexOf( newElements.firstLabel ), &labelRow, &labelColumn, &labelRowSpan, &labelColumnSpan );

	// add new elements under the original label
	newElements.textBrowser = new QTextBrowser;
	newElements.textBrowser->setMinimumSize( 500, 200 );
	newElements.textBrowser->setLineWrapMode( QTextBrowser::LineWrapMode::WidgetWidth );
	layout->addWidget( newElements.textBrowser, labelRow + 1, labelColumn, 1, 1 );

	newElements.secondLabel = new QLabel;
	newElements.secondLabel->setOpenExternalLinks( true );
	layout->addWidget( newElements.secondLabel, labelRow + 2, labelColumn, 1, 1 );

	return newElements;
}

bool showUpdateNotification( QWidget * parent, const QStringList & versionInfo, bool includeCheckbox )
{
	QString newVersion = versionInfo.first();

	// versionInfo contract: [0] version, [1] download URL, [2..] release notes
	QString downloadUrl = versionInfo.size() > 1 ? versionInfo[ 1 ] : QString();
	if (downloadUrl.isEmpty())
		downloadUrl = releasePageUrl;
	QStringList releaseNotes = versionInfo.mid( 2 );

	QMessageBox msgBox( QMessageBox::Information, "Update available", {}, QMessageBox::Ok, parent );

	// On Windows we need to manually make title bar of every new window dark, if dark theme is used.
	themes::updateWindowBorder( &msgBox );

	// add checkbox for automatic update checks
	QCheckBox chkBox( "Check for updates on every start" );
	if (includeCheckbox)
	{
		chkBox.setChecked( true );  // if this was called with includeCheckbox, it must be true
		msgBox.setCheckBox( &chkBox );
	}

	NewElements newElements = reworkLayout( msgBox );

	if (newElements)
	{
		newElements.firstLabel->setText(
			"<html><head/><body>"
			"<p>"
				"Version "%newVersion%" is available."
			"</p><p>"
				"Here is what's new."
			"</p>"
			"</body></html>"
		);

		newElements.textBrowser->setText( releaseNotes.join('\n') );

		newElements.secondLabel->setText(
			"<html><head/><body>"
			"<p>"
				"You can download it at " HYPERLINK( downloadUrl, downloadUrl ) "."
			"</p>"
			"</body></html>"
		);
	}
	else  // layout rework failed
	{
		msgBox.setText(
			"<html><head/><body>"
			"<p>"
				"Version "%newVersion%" is available."
			"</p><p>"
				"You can download it at<br>"
				HYPERLINK( downloadUrl, downloadUrl ) "."
			"</p><p>"
				"Bellow you can see what's new."
			"</p>"
			"</body></html>"
		);

		// show release notes at least in the message box details
		msgBox.setDetailedText( releaseNotes.join('\n') );

		// automatically expand the details section
		const QList< QAbstractButton * > buttons = msgBox.buttons();
		for (QAbstractButton * button : buttons)
		{
			if (button->text().startsWith("Show Details"))
			{
				button->click();
				break;
			}
		}
	}

	msgBox.exec();

	return chkBox.isChecked();
}
