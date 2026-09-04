//======================================================================================================================
// Project: DoomRunnerPlus (shared "common" library)
//----------------------------------------------------------------------------------------------------------------------
// Launch.cpp - assembles the engine launch command.
//======================================================================================================================

#include "Launch.h"

#include <QFileInfo>
#include <QtGlobal>


namespace drp {

static QString resolveIwadPath( const QString & presetIwad, const QList< Iwad > & iwads )
{
	if (presetIwad.isEmpty())
		return QString();

	if (QFileInfo::exists( presetIwad ))
		return presetIwad;

	// the stored value may be an IWAD id - resolve it against the loaded IWAD list
	for (const Iwad & iwad : iwads)
		if (iwad.id == presetIwad)
			return iwad.path;

	return presetIwad;
}

static QStringList buildEngineArgs( const Preset & preset, const QList< Iwad > & iwads )
{
	QStringList args;

	const QString iwad = resolveIwadPath( preset.iwad, iwads );
	if (!iwad.isEmpty())
		args << QStringLiteral("-iwad") << iwad;

	QStringList files = preset.mapPacks;
	files += preset.mods;
	if (!files.isEmpty())
		args << QStringLiteral("-file") << files;

	args += preset.extraArgs;

	return args;
}

LaunchCommand buildPresetLaunch( const Preset & preset, const QList< Engine > & engines, const QList< Iwad > & iwads )
{
	LaunchCommand cmd;

	const Engine * engine = nullptr;
	for (const Engine & e : engines)
		if (e.id == preset.engineId)
		{
			engine = &e;
			break;
		}
	if (!engine)
		return cmd;

	cmd.program = engine->path;
	cmd.args = buildEngineArgs( preset, iwads );

	const bool onSteamDeck = (qgetenv("STEAM_DECK") == "1");
	if (preset.useGamescope || onSteamDeck)
	{
		QStringList gcArgs;
		gcArgs << QStringLiteral("-f");
		if (onSteamDeck)
			gcArgs << QStringLiteral("-e");
		gcArgs << QStringLiteral("--") << cmd.program << cmd.args;
		cmd.program = QStringLiteral("gamescope");
		cmd.args = gcArgs;
	}

	cmd.ok = true;
	return cmd;
}

} // namespace drp
