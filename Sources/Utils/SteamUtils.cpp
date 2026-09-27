//======================================================================================================================
// Project: DoomRunnerPlus
//----------------------------------------------------------------------------------------------------------------------
// Description: adding non-Steam game shortcuts to Steam's shortcuts.vdf
//======================================================================================================================

#include "SteamUtils.hpp"

#include "OSUtils.hpp"    // getMainHomeDir
#include "FileSystemUtils.hpp"  // quoted

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <limits>

#if !IS_WINDOWS && !IS_MACOS  // the whole module only makes sense on Linux, but keep it compilable everywhere
#define STEAM_UTILS_ACTIVE 1
#endif


namespace steam {


#ifdef STEAM_UTILS_ACTIVE

namespace {

//----------------------------------------------------------------------------------------------------------------------
// binary VDF (Valve KeyValues) parsing and serialization

constexpr quint8 TypeMap    = 0x00;  // start of a map (nested node), terminated by the EndOfMap byte
constexpr quint8 TypeString = 0x01;
constexpr quint8 TypeInt32  = 0x02;
constexpr quint8 TypeInt64  = 0x03;  // e.g. LastPlayTime
constexpr quint8 EndOfMap   = 0x08;

struct VdfNode
{
	quint8 type = TypeString;
	QString key;
	QByteArray str;          ///< valid for type == TypeString
	qint64 integer = 0;      ///< valid for numeric types, incl. the special-case "appid" field (TypeMap + int32)
	QVector< VdfNode > children;  ///< valid for type == TypeMap

	bool isMap() const     { return type == TypeMap; }
	bool isString() const  { return type == TypeString; }
};

class VdfReader
{
 public:

	explicit VdfReader( const QByteArray & data ) : _data( data ) {}

	/// Parses the whole document, i.e. the root map with a single "shortcuts" entry.
	VdfNode parseRoot( QString & errorMsg )
	{
		_pos = 0;

		const quint8 marker = readByte( errorMsg );
		if (!errorMsg.isEmpty())
			return {};
		if (marker != TypeMap)
		{
			errorMsg = "unexpected leading byte in the file";
			return {};
		}

		const QString rootKey = readCString( errorMsg );
		if (!errorMsg.isEmpty())
			return {};
		if (rootKey != "shortcuts")
		{
			errorMsg = "expected a \"shortcuts\" map";
			return {};
		}

		VdfNode root;
		root.type = TypeMap;
		root.key = rootKey;
		root.children = readMapBody( errorMsg );
		return root;
	}

 private:

	quint8 readByte( QString & errorMsg )
	{
		if (_pos >= _data.size())
		{
			errorMsg = "unexpected end of file";
			return 0;
		}
		return quint8( _data.at( _pos++ ) );
	}

	QByteArray readBytes( int count, QString & errorMsg )
	{
		if (_pos + count > _data.size())
		{
			errorMsg = "unexpected end of file";
			return {};
		}
		QByteArray bytes = _data.mid( _pos, count );
		_pos += count;
		return bytes;
	}

	QString readCString( QString & errorMsg )
	{
		const int end = _data.indexOf( '\0', _pos );
		if (end < 0)
		{
			errorMsg = "unexpected end of file";
			return {};
		}
		const QString str = QString::fromUtf8( _data.mid( _pos, end - _pos ) );
		_pos = end + 1;
		return str;
	}

	qint64 readInteger( int byteCount, QString & errorMsg )
	{
		const QByteArray bytes = readBytes( byteCount, errorMsg );
		if (!errorMsg.isEmpty())
			return 0;

		qint64 value = 0;
		for (int i = 0; i < byteCount; ++i)
			value |= qint64( quint8( bytes.at( i ) ) ) << ( 8 * i );  // little endian
		return value;
	}

	/// Reads the contents of a map until the EndOfMap byte.
	QVector< VdfNode > readMapBody( QString & errorMsg )
	{
		QVector< VdfNode > children;

		while (true)
		{
			const quint8 marker = readByte( errorMsg );
			if (!errorMsg.isEmpty())
				return {};

			if (marker == EndOfMap)
				return children;

			const QString key = readCString( errorMsg );
			if (!errorMsg.isEmpty())
				return {};

			if (marker == TypeString)
			{
				VdfNode node;
				node.type = TypeString;
				node.key = key;
				node.str = readCString( errorMsg ).toUtf8();
				if (!errorMsg.isEmpty())
					return {};
				children.append( std::move( node ) );
			}
			else if (marker == TypeInt32 || marker == TypeInt64)
			{
				VdfNode node;
				node.type = marker;
				node.key = key;
				node.integer = readInteger( marker == TypeInt32 ? 4 : 8, errorMsg );
				if (!errorMsg.isEmpty())
					return {};
				children.append( std::move( node ) );
			}
			else if (marker == TypeMap)
			{
				if (key == "appid")
				{
					// quirk of shortcuts.vdf: the "appid" field is marked as a map type but holds an int32
					VdfNode node;
					node.type = TypeInt32;
					node.key = key;
					node.integer = readInteger( 4, errorMsg );
					if (!errorMsg.isEmpty())
						return {};
					children.append( std::move( node ) );
				}
				else
				{
					VdfNode node;
					node.type = TypeMap;
					node.key = key;
					node.children = readMapBody( errorMsg );
					if (!errorMsg.isEmpty())
						return {};
					children.append( std::move( node ) );
				}
			}
			else
			{
				errorMsg = QString( "unknown entry type %1 in the file" ).arg( marker );
				return {};
			}
		}
	}

	 QByteArray _data;
	 int _pos = 0;
};

void serializeString( QByteArray & out, const QString & str )
{
	out.append( char( TypeString ) );
	out.append( str.toUtf8() );
	out.append( '\0' );
}

void serializeEntry( QByteArray & out, const VdfNode & entry )
{
	for (const VdfNode & child : entry.children)
	{
		if (child.type == TypeString)
		{
			serializeString( out, child.key );
			out.append( child.str );
			out.append( '\0' );
		}
		else if (child.type == TypeInt32 || child.type == TypeInt64)
		{
			// quirk of shortcuts.vdf: the "appid" field is marked with the map type even though it holds an int32
			const quint8 marker = (child.key == "appid") ? TypeMap : child.type;
			out.append( char( marker ) );
			out.append( child.key.toUtf8() );
			out.append( '\0' );
			const int byteCount = (child.type == TypeInt32) ? 4 : 8;
			for (int i = 0; i < byteCount; ++i)
				out.append( char( quint8( (child.integer >> (8 * i)) & 0xFF ) ) );
		}
		else if (child.type == TypeMap)
		{
			out.append( char( TypeMap ) );
			out.append( child.key.toUtf8() );
			out.append( '\0' );
			serializeEntry( out, child );
			out.append( char( EndOfMap ) );
		}
	}
}

/// The VDF values for Exe and StartDir are stored with surrounding double quotes.
QString quoteVdfPath( const QString & path )
{
	return '"'%path%'"';
}

QString unquoteVdfPath( const QString & path )
{
	if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
		return path.mid( 1, path.size() - 2 );
	return path;
}

VdfNode * findChild( VdfNode & map, const QString & key )
{
	for (VdfNode & child : map.children)
		if (child.key == key)
			return &child;
	return nullptr;
}

VdfNode makeStringNode( const QString & key, const QString & value )
{
	VdfNode node;
	node.type = TypeString;
	node.key = key;
	node.str = value.toUtf8();
	return node;
}

VdfNode makeInt32Node( const QString & key, qint32 value )
{
	VdfNode node;
	node.type = TypeInt32;
	node.key = key;
	node.integer = value;
	return node;
}

VdfNode makeShortcutEntry( const QString & name, const QString & exePath, const QString & startDir, const QString & launchOptions )
{
	VdfNode entry;
	entry.type = TypeMap;
	entry.children = {
		makeInt32Node( "appid", 0 ),           // Steam computes the real app ID from the exe path and name
		makeStringNode( "AppName", name ),
		makeStringNode( "Exe", quoteVdfPath( exePath ) ),
		makeStringNode( "StartDir", quoteVdfPath( startDir ) ),
		makeStringNode( "icon", "" ),
		makeStringNode( "ShortcutPath", "" ),
		makeInt32Node( "IsHidden", 0 ),
		makeInt32Node( "AllowDesktopConfig", 1 ),
		makeInt32Node( "AllowOverlay", 1 ),
		makeInt32Node( "openvr", 0 ),
		makeInt32Node( "LastPlayTime", 0 ),
		makeStringNode( "LaunchOptions", launchOptions ),
	};
	VdfNode tags;
	tags.type = TypeMap;
	tags.key = "tags";
	entry.children.append( std::move( tags ) );
	return entry;
}

//----------------------------------------------------------------------------------------------------------------------
// finding Steam's shortcuts.vdf

QString findShortcutsFile( QString & steamDirOut )
{
	const QString homeDir = os::getMainHomeDir();
	const QStringList steamDirCandidates = {
		homeDir % "/.local/share/Steam",
		homeDir % "/.steam/steam",
		homeDir % "/.steam/debian-installation",
		homeDir % "/.var/app/com.valvesoftware.Steam/data/Steam",  // Steam installed as Flatpak
	};

	QString steamDir;
	for (const QString & candidate : steamDirCandidates)
	{
		if (QFileInfo::exists( candidate % "/userdata" ))
		{
			steamDir = candidate;
			break;
		}
	}
	if (steamDir.isEmpty())
		return {};

	const QDir userdataDir( steamDir % "/userdata" );
	const QStringList userIds = userdataDir.entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );
	if (userIds.isEmpty())
		return {};

	// Prefer the user that already has shortcuts (there is only ever one in practice), otherwise the first one.
	QString shortcutsPath;
	for (const QString & userId : userIds)
	{
		const QString candidate = userdataDir.filePath( userId % "/config/shortcuts.vdf" );
		if (QFileInfo::exists( candidate ))
		{
			shortcutsPath = candidate;
			break;
		}
	}
	if (shortcutsPath.isEmpty())
		shortcutsPath = userdataDir.filePath( userIds.first() % "/config/shortcuts.vdf" );

	steamDirOut = steamDir;
	return shortcutsPath;
}

} // namespace


//----------------------------------------------------------------------------------------------------------------------
// public API

QString addShortcut( const QString & name, const QString & exePath, const QString & startDir, const QString & launchOptions )
{
	QString steamDir;
	const QString shortcutsPath = findShortcutsFile( steamDir );
	if (shortcutsPath.isEmpty())
	{
		return "Steam installation was not found. Is Steam installed and has it been run at least once?";
	}

	// make sure the config dir exists
	const QString configDir = QFileInfo( shortcutsPath ).absolutePath();
	if (!QDir( configDir ).mkpath( "." ))
	{
		return "Cannot create directory "%quoted( configDir );
	}

	// read the current shortcuts, if any
	QByteArray data;
	QFile file( shortcutsPath );
	if (file.exists())
	{
		if (!file.open( QIODevice::ReadOnly ))
		{
			return "Cannot open file "%quoted( shortcutsPath )%" for reading (" % file.errorString() % ")";
		}
		data = file.readAll();
		file.close();
	}

	// parse the current shortcuts
	VdfNode root;  // root map ("shortcuts"), its children are the individual shortcut entries
	if (!data.isEmpty())
	{
		QString parseError;
		VdfReader reader( data );
		root = reader.parseRoot( parseError );
		if (!parseError.isEmpty())
		{
			return "Cannot parse file "%quoted( shortcutsPath )%" (" % parseError % ")";
		}
	}
	else
	{
		root.type = TypeMap;
		root.key = "shortcuts";
	}

	// update an existing entry with the same name and executable, or append a new one
	bool updatedExisting = false;
	for (VdfNode & entry : root.children)
	{
		const VdfNode * appName = findChild( entry, "AppName" );
		const VdfNode * exe = findChild( entry, "Exe" );
		if (!appName || !exe || !appName->isString() || !exe->isString())
			continue;
		if (QString::fromUtf8( appName->str ) == name && unquoteVdfPath( QString::fromUtf8( exe->str ) ) == exePath)
		{
			findChild( entry, "AppName" )->str = name.toUtf8();
			findChild( entry, "Exe" )->str = quoteVdfPath( exePath ).toUtf8();
			findChild( entry, "StartDir" )->str = quoteVdfPath( startDir ).toUtf8();
			VdfNode * launchOpts = findChild( entry, "LaunchOptions" );
			if (launchOpts && launchOpts->isString())
				launchOpts->str = launchOptions.toUtf8();
			updatedExisting = true;
			break;
		}
	}
	if (!updatedExisting)
	{
		VdfNode entry = makeShortcutEntry( name, exePath, startDir, launchOptions );
		entry.key = QString::number( root.children.size() );
		root.children.append( std::move( entry ) );
	}

	// serialize and write back
	QByteArray out;
	out.append( char( TypeMap ) );
	out.append( root.key.toUtf8() );
	out.append( '\0' );
	serializeEntry( out, root );
	out.append( char( EndOfMap ) );

	if (!file.open( QIODevice::WriteOnly | QIODevice::Truncate ))
	{
		return "Cannot open file "%quoted( shortcutsPath )%" for writing (" % file.errorString() % ")";
	}
	if (file.write( out ) != out.size())
	{
		return "Error writing to file "%quoted( shortcutsPath )%" (" % file.errorString() % ")";
	}

	return {};
}

#else  // non-Linux platforms

QString addShortcut( const QString & /*name*/, const QString & /*exePath*/, const QString & /*startDir*/, const QString & /*launchOptions*/ )
{
	return "This feature only works on Linux.";
}

#endif


} // namespace steam
