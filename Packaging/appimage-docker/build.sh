#!/bin/bash
# Builds a Steam Deck-compatible AppImage for Doom Runner Plus by building it inside
# an Ubuntu 24.04 container (glibc 2.39). Requires Docker.
#
# The produced AppImage is written into <repo>/Releases/ so the host can upload it.
#
# Usage: ./build.sh

set -euo pipefail

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
REPO_DIR="$(realpath "$SCRIPT_DIR/../..")"
IMAGE="doomrunner-appimage-builder"

echo "==> Building the AppImage build container..."
docker build -q -t "$IMAGE" "$SCRIPT_DIR"

echo "==> Building the AppImage inside the container..."
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -e HOME=/home/builder \
  -e NO_STRIP=1 \
  -e APPIMAGE_EXTRACT_AND_RUN=1 \
  -v "$REPO_DIR:/app/DoomRunner" \
  -w /app/DoomRunner \
  "$IMAGE" \
  bash -lc '
    set -e
    bash Scripts/1-build.sh default dynamic plain release
    source /tmp/DoomRunner/build_vars.sh
    bash Scripts/2-package.sh "$BUILD_DIR" Linux x86_64 appimage
    bash Scripts/2-package.sh "$BUILD_DIR" Linux x86_64 dynamic_exe
  '

echo "==> Done. AppImage:"
ls -1 "$REPO_DIR"/Releases/*.AppImage
