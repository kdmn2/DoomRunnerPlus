# Builds a Steam Deck-compatible AppImage for DoomRunnerSD (the minimal QML launcher).
#
# Same base-image rationale as the Doom Runner Plus AppImage build: SteamOS ships glibc 2.39,
# so we build on Ubuntu 24.04 (glibc 2.39) and bundle a matching libssl to keep the Qt TLS
# backend working. This image additionally installs the Qt Quick / QML modules.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    APPIMAGE_EXTRACT_AND_RUN=1 \
    NO_STRIP=1 \
    PATH="/usr/lib/qt6/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/home/builder/Apps"

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential make g++ \
      qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
      libqt6qml6 libqt6quick6 qt6-wayland \
      qml6-module-qtqml qml6-module-qtquick qml6-module-qtquick-window \
      qml6-module-qtquick-templates qml6-module-qtqml-workerscript \
      libgl1-mesa-dev libglx-dev libopengl-dev \
      file python3 wget ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /home/builder/Apps \
    && wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -O /home/builder/Apps/linuxdeploy-x86_64.AppImage \
    && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage -O /home/builder/Apps/linuxdeploy-plugin-qt-x86_64.AppImage \
    && wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O /home/builder/Apps/appimagetool-x86_64.AppImage \
    && chmod +x /home/builder/Apps/linuxdeploy-x86_64.AppImage /home/builder/Apps/linuxdeploy-plugin-qt-x86_64.AppImage /home/builder/Apps/appimagetool-x86_64.AppImage
