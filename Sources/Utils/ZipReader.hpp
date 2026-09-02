//======================================================================================================================
// Project: DoomRunner
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: zip file parsing
//======================================================================================================================

#ifndef ZIP_READER_INCLUDED
#define ZIP_READER_INCLUDED


#include "Essential.hpp"

#include "FileInfoCacheTypes.hpp"  // ReadStatus
#include "LangUtils.hpp"           // ValueOrError

#include <QString>
#include <QStringList>
#include <QByteArray>


using UncertainFileContent = ValueOrError<QByteArray, ReadStatus, ReadStatus::Success>;

/// Extracts the content of the first of innerFileNames that is found within the zip file.
/** BEWARE that this operation may be very time consuming, depending on the size of the file and level of compression.
  * Doing this asynchronously is adviced.
  * The returned status will be NotFound when the zip file is not found,
  * but InfoNotPresent when none of the innerFileNames is found. */
UncertainFileContent readOneOfFilesInsideZip( const QString & zipFilePath, const QStringList & innerFileNames );

/// Extracts all files from a zip archive into targetDir, preserving the archive's internal directory structure.
/** BEWARE that this operation may be very time consuming, depending on the size of the file and level of compression.
  * Entry paths are sanitized so that no file is written outside of targetDir.
  * Returns false if the archive cannot be opened or any entry cannot be extracted. */
bool extractZipArchive( const QString & zipFilePath, const QString & targetDir );


#endif // ZIP_READER_INCLUDED
