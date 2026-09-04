# Steam Deck-compatible AppImage build

The AppImage must be built on a distribution whose glibc is **not newer** than the one
on the target system. Steam Deck (SteamOS) ships **glibc 2.39**, so building on the newest
rolling distros (glibc 2.44 on CachyOS/Arch) produces an AppImage whose bundled Qt /
glib / curl libraries require `GLIBC_2.43` and fail to load on the Deck:

```
DoomRunnerPlus: /usr/lib/libm.so.6: version `GLIBC_2.43' not found (required by .../libQt6Gui.so.6)
```

This folder builds the AppImage inside an **Ubuntu 24.04** container (glibc 2.39, Qt 6.4),
which runs on the Deck.

## Requirements

- Docker (any machine with Docker; the Steam Deck itself is not a suitable build host)

## Usage

```bash
./build.sh
```

The AppImage is written into `<repo>/Releases/`.

> Note: this is built for the project only; the "build scripts" (Scripts/1-build.sh,
> 2-package.sh) are reused inside the container.
