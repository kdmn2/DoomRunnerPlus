//======================================================================================================================
// Project: DoomRunnerSD
//----------------------------------------------------------------------------------------------------------------------
// LauncherBackend - bridges the QML UI to the shared "common" config/launch library.
//======================================================================================================================

#include "LauncherBackend.h"

#include "Launch.h"

#include <QProcess>


LauncherBackend::LauncherBackend( QObject * parent )
	: QObject( parent )
{
	configPath_ = drp::findConfigFile();
	drp::readConfig( configPath_, engines_, iwads_, presets_ );
}

QVariantList LauncherBackend::presets() const
{
	QVariantList result;
	for (const drp::Preset & preset : presets_)
		result.append( preset.name );
	return result;
}

QString LauncherBackend::configPath() const { return configPath_; }

QString LauncherBackend::commandFor( int presetIndex ) const
{
	if (presetIndex < 0 || presetIndex >= presets_.size())
		return QString();

	const drp::LaunchCommand cmd = drp::buildPresetLaunch( presets_[ presetIndex ], engines_, iwads_ );
	if (!cmd.ok)
		return QStringLiteral("<engine not found>");

	QStringList full;
	full << cmd.program << cmd.args;
	return full.join( ' ' );
}

bool LauncherBackend::launchPreset( int presetIndex )
{
	if (presetIndex < 0 || presetIndex >= presets_.size())
		return false;

	const drp::LaunchCommand cmd = drp::buildPresetLaunch( presets_[ presetIndex ], engines_, iwads_ );
	if (!cmd.ok)
	{
		emit errorOccurred( "The selected preset's engine could not be found in the config." );
		return false;
	}

	return QProcess::startDetached( cmd.program, cmd.args );
}
