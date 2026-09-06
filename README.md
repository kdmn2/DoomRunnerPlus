# DoomRunnerPlus

DoomRunnerPlus is a fork of [Doom Runner](https://github.com/Youda008/DoomRunner) — a graphical launcher for common Doom source ports (GZDoom, Zandronum, Crispy Doom, PrBoom, Woof, DSDA-Doom, Nugget-Doom, ...), written in C++ with the Qt framework and built around one-click presets for multi-file mods.

This fork was made so the launcher works nicely on a **Steam Deck** and so you can **search for and download WADs** without leaving the app.

## ⚠️ Important: how this was made

This project was produced **entirely with the help of AI** (an AI coding agent). The author has **no coding skills** and only wanted to make a few targeted changes so the original launcher works on the Steam Deck and so WADs can be downloaded from within the app.

Because of this, there is no guarantee of code quality, correctness, or that everything is bug-free. **Use it at your own risk.** The original Doom Runner, on which this is based, is a solid and well-made project — all the credit for that goes to its author.

## What's new vs. original Doom Runner

* **WAD Downloader** — browse and download WADs from the [/idgames archive](https://www.doomworld.com/idgames/), the [Cacowards](https://www.doomworld.com/cacowards/) and [Top WADs](https://www.doomworld.com/topwads/) lists, with built-in search, a shared activity log and sortable result columns.
* **Steam Deck / gamescope support** — fixes the "nested gamescope" crash when running in Steam game mode, adds a configurable gamescope-arguments field, and makes the UI stay legible on the Deck's screen.
* **Touch-friendly UI** — a larger Launch button and a word-wrapped, multi-line "final launch command" display.
* **Adjustable font size** — the whole UI (including the main window) can be scaled up or down from the Initial Setup window.
* **Controller navigation** — a gamepad can drive the launcher directly (D-pad/stick to move, A/B to activate/back, shoulder buttons to switch tabs) via SDL2.
* **Gamepad support for source ports** — a per-preset toggle that passes the correct flag per engine family: `+joy_enable 1` for ZDoom-family ports (GZDoom, UZDoom, ...), `-assign use_game_controller=1` for dsda-doom (and `-nojoy` when off); Woof / Nugget-Doom have controller support on by default.
* **Engine fixes** — launch engines by their full path (instead of a `./name` relative path), load PWADs with the correct `-file` flag (not `-merge`), load BEX patches via `-deh`, and correctly classify Woof as an MBF-family port.
* **Launch reliability** — a guard against accidentally launching two copies of the same engine, plus a diagnostic that shows the engine's console output if it exits within the first ~5 seconds (e.g. when a WAD fails to load).
* **Automated release packages** — Linux AppImage + plain zip + Flatpak, Windows zip, and macOS DMGs (arm64 + x86_64), all built by GitHub Actions and published to the Releases page.

## Download

Pre-built packages are on the [Releases page](https://github.com/kdmn2/DoomRunnerPlus/releases):

* **Linux:**
  * `DoomRunner-<version>-Linux-x86_64.AppImage` — portable bundle, works on the Steam Deck.
  * `DoomRunner-<version>-Linux-x86_64-dynamic_exe.zip` — plain binary.
  * `DoomRunner-<version>-Linux-x86_64-app.flatpak` — Flatpak bundle (and a `-symbols` debug variant).
* **Windows:** `DoomRunner-<version>-Windows-x86_64-static_exe.zip`
* **macOS:** `DoomRunner-<version>-MacOS-<arch>.dmg` for both `arm64` and `x86_64`

## License

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE). As a fork, it builds on the work of the original Doom Runner, which is also GPLv3.

**Qt acknowledgment.** This application is written with the help of the [Qt framework](https://www.qt.io/). Qt's license requires that its use be publicly acknowledged and its source code be made accessible, which can be found here: https://github.com/qt/qtbase. The exact Qt version is not fixed and can vary based on your operating system or distribution; it should work with anything from Qt 5.15 up to the latest 6.x.

## Credit / original project

The original Doom Runner and its documentation are at https://github.com/Youda008/DoomRunner. The bulk of the launcher's functionality, ideas and original code belong to its author, **Youda008** — this fork only adds the changes listed above, assembled with AI assistance as described above.
