#======================================================================================================================
# DoomRunnerSD - a minimal, controller-first launcher for Doom Runner Plus, intended for Steam Deck / Big Picture.
# It reads the config file produced by Doom Runner Plus (via the shared "common" library) and launches a preset.
#======================================================================================================================

QT += qml quick
QT += core gui

CONFIG += c++17
CONFIG -= app_bundle

TARGET = DoomRunnerSD
TEMPLATE = app

# the shared config-reading / launch-building library
include(../common/common.pri)

SOURCES += main.cpp \
	LauncherBackend.cpp

HEADERS += LauncherBackend.h

RESOURCES += qml.qrc
