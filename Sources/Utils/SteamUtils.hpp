//======================================================================================================================
// Project: DoomRunnerPlus
//----------------------------------------------------------------------------------------------------------------------
// Description: adding non-Steam game shortcuts to Steam's shortcuts.vdf
//----------------------------------------------------------------------------------------------------------------------
// How it works:
//   Steam stores manually added non-Steam games in
//   <steam_dir>/userdata/<user_id>/config/shortcuts.vdf, which is a binary Valve's KeyValues (VDF) file.
//   We parse it, append (or update) a shortcut entry and write it back, which has the same effect as using
//   Steam's own "Add to Steam" / "Add a Non-Steam Game" dialog. The change is picked up when Steam restarts.
//======================================================================================================================

#ifndef STEAM_UTILS_INCLUDED
#define STEAM_UTILS_INCLUDED

#include "Essential.hpp"

#include <QString>


namespace steam {


/// Adds a non-Steam game shortcut with the given name to Steam's list of shortcuts.
/** The shortcut will point to the given executable with the given launch options (command line arguments)
  * and the given start directory as working directory.
  * If a shortcut with the same name and executable already exists, it is updated.
  * Returns an empty string on success, or an error message on failure. */
QString addShortcut( const QString & name, const QString & exePath, const QString & startDir, const QString & launchOptions );


} // namespace steam


#endif // STEAM_UTILS_INCLUDED
