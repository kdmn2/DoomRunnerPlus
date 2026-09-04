//======================================================================================================================
// Project: DoomRunnerPlus (shared "common" library)
//----------------------------------------------------------------------------------------------------------------------
// Description: builds the engine launch command for a preset (+ the gamescope wrap used on Steam Deck).
//======================================================================================================================

#ifndef DRP_COMMON_LAUNCH_H
#define DRP_COMMON_LAUNCH_H

#include "Config.h"

#include <QString>
#include <QStringList>


namespace drp {

/// The fully assembled command to run (program + args), ready for QProcess::startDetached.
struct LaunchCommand
{
	QString program;
	QStringList args;
	bool ok = false;
};

/// Builds the launch command for a preset: engine + -iwad + -file <map packs/mods> + the preset's extra args,
/// wrapped in a gamescope instance when running on a Steam Deck or when the preset asks for it.
LaunchCommand buildPresetLaunch( const Preset & preset, const QList< Engine > & engines, const QList< Iwad > & iwads );

} // namespace drp

#endif // DRP_COMMON_LAUNCH_H
