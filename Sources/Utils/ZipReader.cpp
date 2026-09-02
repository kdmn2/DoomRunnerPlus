//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: zip file parsing
//======================================================================================================================

#include "ZipReader.hpp"

#include "LangUtils.hpp"        // autoClosable
#include "StringUtils.hpp"      // operator<<( QTextStream &, const QStringList & )
#include "FileSystemUtils.hpp"  // isValidFile
#include "ErrorHandling.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <minizip/unzip.h>


//======================================================================================================================
// implementation

namespace {

/// Normalizes a zip entry path so that it can be safely joined to a target directory.
/** Returns an empty string for entries that would escape the target directory (absolute paths,
  * path-traversal segments, or Windows drive prefixes). */
QString safeEntryPath( const QString & entryName )
{
	QString path = entryName;
	path.replace( '\\', '/' );
	while (path.startsWith( '/' ))
		path.remove( 0, 1 );

	if (path.split( '/' ).contains( QStringLiteral("..") ))
		return QString();
	if (path.size() >= 2 && path[ 0 ].isLetter() && path[ 1 ] == ':')
		return QString();  // Windows drive prefix

	return path;
}

} // namespace

// logging helper
class LoggingZipReader : protected LoggingComponent {

 public:

	LoggingZipReader( QString filePath ) : LoggingComponent( u"ZipReader" ), _filePath( std::move(filePath) ) {}

	UncertainFileContent readOneOfFilesInsideZip( const QStringList & innerFileNames );

 private:

	QString _filePath;

};

UncertainFileContent LoggingZipReader::readOneOfFilesInsideZip( const QStringList & innerFileNames )
{
	// we need a distinguishable error code when the file does not exist
	if (!fs::isValidFile( _filePath ))
	{
		return ReadStatus::NotFound;
	}

	// open the zip file
	unzFile zipFile = unzOpen( _filePath.toUtf8().constData() );
	if (!zipFile)
	{
		logRuntimeError() << "Cannot open "<<_filePath;
		return ReadStatus::CantOpen;
	}
	auto zipFileGuard = autoClosable( zipFile, unzClose );

	// find one of innerFileNames
	QString foundInnerFileName;
	for (const QString & innerFileName : innerFileNames)
	{
		if (unzLocateFile( zipFile, innerFileName.toUtf8().constData(), 0 ) == UNZ_OK)
		{
			foundInnerFileName = innerFileName;
			break;
		}
	}
	if (foundInnerFileName.isNull())
	{
		logDebug() << "Couldn't find "<<innerFileNames<<" within "<<_filePath;
		return ReadStatus::InfoNotPresent;
	}

	// get metadata about the selected inner file
	unz_file_info fileInfo;
	char unused [256];
	if (unzGetCurrentFileInfo( zipFile, &fileInfo, unused, sizeof(unused), nullptr, 0, nullptr, 0 ) != UNZ_OK)
	{
		logRuntimeError() << "Failed to get file info of "<<foundInnerFileName<<" within "<<_filePath;
		return ReadStatus::CantOpen;
	}

	// safety check - don't try to decompress a file that is nonsensically large
	if (fileInfo.uncompressed_size > 10*1024*1024)
	{
		logRuntimeError() << "Refusing to read file "<<foundInnerFileName<<" within "<<_filePath
		                  << ", because it is too large ("<<fileInfo.uncompressed_size<<" bytes)";
		return ReadStatus::FailedToRead;
	}
	int uncompressedSize = static_cast< int >( fileInfo.uncompressed_size );

	// open the selected inner file for reading
	if (unzOpenCurrentFile( zipFile ) != UNZ_OK)
	{
		logRuntimeError() << "Failed to open file "<<foundInnerFileName<<" within "<<_filePath;
		return ReadStatus::CantOpen;
	}
	auto currentFileGuard = atScopeEndDo( [ &zipFile ]() { unzCloseCurrentFile( zipFile ); } );

	// decompress and read the inner file
	QByteArray buffer;
	buffer.resize( uncompressedSize );
	int bytesRead = unzReadCurrentFile( zipFile, buffer.data(), buffer.size() );
	if (bytesRead < 0)
	{
		logRuntimeError() << "Failed to read file "<<foundInnerFileName<<" within "<<_filePath;
		return ReadStatus::FailedToRead;
	}
	else if (bytesRead < uncompressedSize)
	{
		logRuntimeError() << "Couldn't read the whole file "<<foundInnerFileName<<" within "<<_filePath
		                  << " (read only "<<bytesRead<<" bytes)";
		buffer.resize( bytesRead );
	}
	return buffer;
}


//======================================================================================================================
// public API

UncertainFileContent readOneOfFilesInsideZip( const QString & zipFilePath, const QStringList & innerFileNames )
{
	LoggingZipReader zipReader( zipFilePath );
	return zipReader.readOneOfFilesInsideZip( innerFileNames );
}

bool extractZipArchive( const QString & zipFilePath, const QString & targetDir )
{
	unzFile zipFile = unzOpen( zipFilePath.toUtf8().constData() );
	if (!zipFile)
		return false;
	auto zipFileGuard = autoClosable( zipFile, unzClose );

	const QDir outDir( targetDir );
	if (!outDir.exists() && !outDir.mkpath( "." ))
		return false;

	if (unzGoToFirstFile( zipFile ) != UNZ_OK)
		return true;  // empty archive

	do
	{
		char nameBuffer [4096];
		unz_file_info64 fileInfo;
		if (unzGetCurrentFileInfo64( zipFile, &fileInfo, nameBuffer, sizeof(nameBuffer), nullptr, 0, nullptr, 0 ) != UNZ_OK)
			return false;

		const QString entryName = safeEntryPath( QString::fromUtf8( nameBuffer ) );
		if (entryName.isEmpty())
			continue;  // unsafe entry, skip it

		if (entryName.endsWith( '/' ))
		{
			if (!outDir.mkpath( entryName ))
				return false;
			continue;
		}

		const QString outPath = outDir.filePath( entryName );

		// make sure the parent directory exists
		if (!QDir().mkpath( QFileInfo( outPath ).absolutePath() ))
			return false;

		if (unzOpenCurrentFile( zipFile ) != UNZ_OK)
			return false;
		auto currentFileGuard = atScopeEndDo( [ &zipFile ]() { unzCloseCurrentFile( zipFile ); } );

		QFile outFile( outPath );
		if (!outFile.open( QIODevice::WriteOnly ))
			return false;

		char buffer [64 * 1024];
		int bytesRead;
		while ((bytesRead = unzReadCurrentFile( zipFile, buffer, sizeof(buffer) )) > 0)
		{
			if (outFile.write( buffer, bytesRead ) != bytesRead)
			{
				outFile.close();
				return false;
			}
		}
		outFile.close();

		if (bytesRead < 0)
			return false;
	}
	while (unzGoToNextFile( zipFile ) == UNZ_OK);

	return true;
}
