# Builds a Doom Runner Plus AppImage on Ubuntu 24.04 (glibc 2.39).
#
# Why this base? An AppImage must not bundle libraries that require a newer glibc
# than the target system has. Steam Deck (SteamOS) ships glibc 2.39 or older, so
# building on the newest rolling distros (e.g. glibc 2.44) makes an AppImage that
# fails on the Deck with errors like "version `GLIBC_2.43' not found".
# Ubuntu 24.04 is old enough to run on the Deck but still ships Qt 6.4.
#
# Usage: ./build.sh        (from a machine with Docker installed)

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    # AppImage tools fail without FUSE in a container, so let them self-extract.
    APPIMAGE_EXTRACT_AND_RUN=1 \
    # The bundled strip in linuxdeploy can't handle Qt's .relr.dyn sections.
    NO_STRIP=1 \
    PATH="/usr/lib/qt6/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/home/builder/Apps"

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential make g++ \
      qt6-base-dev qt6-base-dev-tools libqt6concurrent6 libqt6network6 \
      libminizip-dev zlib1g-dev \
      libgl1-mesa-dev libglx-dev libopengl-dev \
      p7zip-full \
      file python3 wget ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /home/builder/Apps \
    && wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O /home/builder/Apps/linuxdeploy-x86_64.AppImage \
    && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage -O /home/builder/Apps/linuxdeploy-plugin-qt-x86_64.AppImage \
    && chmod +x /home/builder/Apps/linuxdeploy-x86_64.AppImage /home/builder/Apps/linuxdeploy-plugin-qt-x86_64.AppImage
