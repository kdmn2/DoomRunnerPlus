//======================================================================================================================
// Project: DoomRunnerSD
//----------------------------------------------------------------------------------------------------------------------
// Description: thin QObject bridge between the QML UI and the shared "common" library.
//======================================================================================================================

#ifndef LAUNCHER_BACKEND_H
#define LAUNCHER_BACKEND_H

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QStringList>
#include <QList>

#include "Config.h"


class LauncherBackend : public QObject {

	Q_OBJECT

 public:

	explicit LauncherBackend( QObject * parent = nullptr );

	/// Returns a list of preset names, in the order they appear in the config.
	Q_INVOKABLE QVariantList presets() const;

	/// Builds (but does not run) the launch command for the given preset (for display / debugging).
	Q_INVOKABLE QString commandFor( int presetIndex ) const;

	/// Launches the engine for the given preset. Returns false on failure (no engine / invalid preset).
	Q_INVOKABLE bool launchPreset( int presetIndex );

	Q_INVOKABLE QString configPath() const;

 signals:

	void errorOccurred( const QString & message );

 private:

	QString configPath_;
	QList< drp::Engine > engines_;
	QList< drp::Iwad > iwads_;
	QList< drp::Preset > presets_;

};


#endif // LAUNCHER_BACKEND_H
