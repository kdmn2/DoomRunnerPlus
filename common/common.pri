# Shared DoomRunnerPlus "common" library.
# Include this from an app's .pro file to compile the shared config-reading / launch-building sources:
#     include(../common/common.pri)

INCLUDEPATH += $$PWD

SOURCES += $$PWD/Config.cpp \
	$$PWD/Launch.cpp

HEADERS += $$PWD/Config.h \
	$$PWD/Launch.h
