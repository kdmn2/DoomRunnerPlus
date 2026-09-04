//======================================================================================================================
// Project: DoomRunnerPlus (shared "common" library)
//----------------------------------------------------------------------------------------------------------------------
// Config.cpp - reads options.json into the simple launch model.
//======================================================================================================================

#include "Config.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QRegularExpression>


namespace drp {

QString findConfigFile()
{
	// allow an explicit override (handy for testing)
	const QByteArray env = qgetenv("DRP_CONFIG");
	if (!env.isEmpty())
	{
		const QString path = QString::fromLocal8Bit( env );
		if (QFile::exists( path ))
			return path;
	}

	// Doom Runner Plus stores options.json in <AppData parent>/DoomRunnerPlus/options.json
	QStringList candidates;
	const QString appData = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
	if (!appData.isEmpty())
		candidates << QFileInfo( appData ).dir().filePath( "DoomRunnerPlus/options.json" );
	candidates << QDir::home().filePath( ".local/share/DoomRunnerPlus/options.json" );
	candidates << QDir::home().filePath( ".config/DoomRunnerPlus/options.json" );

	for (const QString & candidate : candidates)
		if (QFile::exists( candidate ))
			return candidate;

	return QString();
}

bool readConfig( const QString & path, QList< Engine > & engines, QList< Iwad > & iwads, QList< Preset > & presets )
{
	engines.clear();
	iwads.clear();
	presets.clear();

	if (path.isEmpty())
		return false;

	QFile file( path );
	if (!file.open( QIODevice::ReadOnly ))
		return false;

	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
	if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		return false;

	const QJsonObject root = doc.object();

	// engines -----------------------------------------------------------------
	const QJsonObject enginesJs = root.value( "engines" ).toObject();
	for (const QJsonValue & value : enginesJs.value( "engine_list" ).toArray())
	{
		const QJsonObject obj = value.toObject();
		if (obj.value( "separator" ).toBool( false ))
			continue;

		Engine engine;
		engine.id   = obj.value( "id" ).toString();
		engine.name = obj.value( "name" ).toString();
		engine.path = obj.value( "path" ).toString();
		if (engine.id.isEmpty() || engine.path.isEmpty())
			continue;

		engines.append( engine );
	}

	// IWADs -------------------------------------------------------------------
	if (root.value( "IWADs" ).isObject())
	{
		for (const QJsonValue & value : root.value( "IWADs" ).toObject().value( "IWAD_list" ).toArray())
		{
			const QJsonObject obj = value.toObject();
			const QString id   = obj.value( "id" ).toString();
			const QString path = obj.value( "path" ).toString();
			if (!path.isEmpty())
				iwads.append( Iwad{ id.isEmpty() ? path : id, path } );  // allowing resolution by id or path
		}
	}

	// presets -----------------------------------------------------------------
	for (const QJsonValue & value : root.value( "presets" ).toArray())
	{
		const QJsonObject obj = value.toObject();
		if (obj.value( "separator" ).toBool( false ))
			continue;

		Preset preset;
		preset.name      = obj.value( "name" ).toString();
		preset.engineId  = obj.value( "selected_engine" ).toString();
		preset.iwad      = obj.value( "selected_IWAD" ).toString();
		preset.useGamescope = obj.value( "use_gamescope" ).toBool( false );

		for (const QJsonValue & mv : obj.value( "selected_mappacks" ).toArray())
			preset.mapPacks << mv.toString();

		for (const QJsonValue & mv : obj.value( "mods" ).toArray())
		{
			const QJsonObject mod = mv.toObject();
			QString path = mod.value( "path" ).toString();
			if (path.isEmpty()) path = mod.value( "file" ).toString();
			if (path.isEmpty()) path = mod.value( "filePath" ).toString();
			if (!path.isEmpty())
				preset.mods << path;
		}

		switch (int( obj.value( "name" ).toBool( false ) )) {}  // no-op: keep parser simple

		static const QRegularExpression argSplit( QStringLiteral("\\s+") );
		const QString extra = obj.value( "additional_args" ).toString();
		if (!extra.trimmed().isEmpty())
			preset.extraArgs = extra.trimmed().split( argSplit, Qt::SkipEmptyParts );

		if (preset.name.isEmpty() || preset.engineId.isEmpty())
			continue;

		presets.append( preset );
	}

	return true;
}

} // namespace drp
