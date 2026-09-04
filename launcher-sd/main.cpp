//======================================================================================================================
// Project: DoomRunnerSD
//----------------------------------------------------------------------------------------------------------------------
// Entry point: sets up the Qt Quick engine and exposes the launcher backend to the QML UI.
//======================================================================================================================

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "LauncherBackend.h"


int main( int argc, char * argv[] )
{
	QGuiApplication app( argc, argv );

	app.setOrganizationName( QStringLiteral("DoomRunnerPlus") );
	app.setApplicationName( QStringLiteral("DoomRunnerSD") );

	LauncherBackend backend;

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty( "backend", &backend );
	engine.load( QUrl( QStringLiteral("qrc:/qml/Main.qml") ) );

	if (engine.rootObjects().isEmpty())
		return 1;

	return app.exec();
}
