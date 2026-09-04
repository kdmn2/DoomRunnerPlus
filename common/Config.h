//======================================================================================================================
// Project: DoomRunnerPlus (shared "common" library)
//----------------------------------------------------------------------------------------------------------------------
// Description: reads the Doom Runner Plus options.json into a simple, launch-oriented model. Used by both the desktop
//              app and the Steam Deck launcher so that the config format lives in one place.
//======================================================================================================================

#ifndef DRP_COMMON_CONFIG_H
#define DRP_COMMON_CONFIG_H

#include <QString>
#include <QStringList>
#include <QList>


namespace drp {

/// A Doom source port entry as stored in the config.
struct Engine
{
	QString id, name, path;
};

/// An IWAD entry as stored in the config.
struct Iwad
{
	QString id, path;
};

/// A launch preset. This is the "simple" subset needed to launch it (not the full editor model).
struct Preset
{
	QString name;
	QString engineId;
	QString iwad;               // IWAD id or absolute path, as stored
	QStringList mapPacks;       // absolute paths
	QStringList mods;           // absolute paths
	QStringList extraArgs;      // the preset's additional command-line args
	bool useGamescope = false;
};

/// Locates the Doom Runner Plus options.json (honors $DRP_CONFIG, then common install dirs).
QString findConfigFile();

/// Parses options.json into the simple launch model. Returns false if the file is missing/invalid.
bool readConfig( const QString & path, QList< Engine > & engines, QList< Iwad > & iwads, QList< Preset > & presets );

} // namespace drp

#endif // DRP_COMMON_CONFIG_H
