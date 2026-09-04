#!/bin/bash
# Builds a Steam Deck-compatible AppImage for DoomRunnerSD inside an Ubuntu 24.04 container.
# The produced AppImage is written into <repo>/Releases/ so the host can upload it.
#
# Usage: ./build.sh

set -euo pipefail

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
REPO_DIR="$(realpath "$SCRIPT_DIR/../../..")"
IMAGE="doomrunner-sd-appimage-builder"

echo "==> Building the DoomRunnerSD AppImage container..."
docker build -q -t "$IMAGE" "$SCRIPT_DIR"

echo "==> Building the launcher inside the container..."
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -e HOME=/home/builder \
  -e NO_STRIP=1 \
  -e APPIMAGE_EXTRACT_AND_RUN=1 \
  -v "$REPO_DIR:/app/DoomRunner" \
  -w /app/DoomRunner/launcher-sd \
  "$IMAGE" \
  bash -lc '
    set -e
    rm -rf "$PWD/AppDir" "$PWD/squashfs-root"
    qmake6
    make -j 10

    export QMAKE=$(which qmake6)   # required by the qt plugin of linuxdeploy

    # Stage everything with linuxdeploy into an AppImage (binary + Qt libs + plugins).
    /home/builder/Apps/linuxdeploy-x86_64.AppImage \
      --executable "$PWD/DoomRunnerSD" \
      --desktop-file /app/DoomRunner/Install/XDG/DoomRunnerSD.desktop \
      --icon-file /app/DoomRunner/Install/XDG/DoomRunner.128x128.png \
      --icon-filename DoomRunnerSD \
      --appdir "$PWD/AppDir" \
      --plugin qt \
      --output appimage

    # linuxdeploy cannot auto-detect QML modules imported from a qrc-embedded Main.qml,
    # so unpack the produced AppImage, add the QML modules + a correct AppRun, then re-pack.
    APPIMG="$(ls "$PWD"/*-x86_64.AppImage | head -n1)"
    rm -rf "$PWD/squashfs-root"
    APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMG" --appimage-extract

    mkdir -p squashfs-root/usr/qml
    cp -a /usr/lib/x86_64-linux-gnu/qt6/qml/. squashfs-root/usr/qml/
    # AppRun is a symlink to the binary; --remove-destination replaces it with a real script
    # instead of writing through the symlink and clobbering usr/bin/DoomRunnerSD.
    cp --remove-destination /app/DoomRunner/Packaging/appimage-docker/launcher-sd/AppRun squashfs-root/AppRun
    chmod +x squashfs-root/AppRun

    /home/builder/Apps/appimagetool-x86_64.AppImage --appimage-extract-and-run \
      squashfs-root /app/DoomRunner/Releases/DoomRunnerSD-Linux-x86_64.AppImage
  '

echo "==> Done. AppImage:"
ls -1 "$REPO_DIR"/Releases/*DoomRunnerSD*.AppImage
