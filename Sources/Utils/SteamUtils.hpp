//======================================================================================================================
// Project: DoomRunnerPlus
//----------------------------------------------------------------------------------------------------------------------
// Description: adding non-Steam game shortcuts to Steam's shortcuts.vdf
//----------------------------------------------------------------------------------------------------------------------
// How it works:
//   Steam stores manually added non-Steam games in
//   <steam_dir>/userdata/<user_id>/config/shortcuts.vdf, which is a binary Valve's KeyValues (VDF) file.
//   We append a new shortcut entry to it, which has the same effect as using Steam's own
//   "Add to Steam" / "Add a Non-Steam Game" dialog. The change is picked up when Steam restarts.
//
// IMPORTANT SAFETY NOTE:
//   Existing entries are NEVER modified. The file is parsed only to count the entries (for the index key)
//   and to check whether a shortcut with the same name already exists. A new entry is then appended as raw
//   bytes right before the final 0x08 terminator, so the original content stays byte-for-byte intact.
//   The file is replaced atomically (QSaveFile), so a crash can never leave a half-written file.
//======================================================================================================================

#ifndef STEAM_UTILS_INCLUDED
#define STEAM_UTILS_INCLUDED

#include "Essential.hpp"

#include <QString>


namespace steam {


/// Returns whether the main Steam client process is currently running.
/** Steam reads its shortcuts only at startup and saves them back from memory when it exits,
  * so a shortcut added while Steam is running may not appear or may get overwritten. */
bool isSteamRunning();


/// Adds a non-Steam game shortcut with the given name to Steam's list of shortcuts.
/** The shortcut will point to the given executable with the given launch options (command line arguments)
  * and the given start directory as working directory.
  * If a shortcut with the same name already exists, the shortcuts file is left completely untouched
  * and `alreadyExisted` is set to true.
  * Returns an empty string on success, or an error message on failure. */
QString addShortcut( const QString & name, const QString & exePath, const QString & startDir, const QString & launchOptions, bool * alreadyExisted = nullptr );


} // namespace steam


#endif // STEAM_UTILS_INCLUDED