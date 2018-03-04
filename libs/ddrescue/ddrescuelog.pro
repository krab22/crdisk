TARGET = ddrescuelog
TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

DEPENDPATH  += $$PWD
INCLUDEPATH += $$PWD/1.23

MOC_DIR     += $$PWD/tmp
OBJECTS_DIR += $$PWD/tmp

SOURCES += 1.23/ddrescuelog.cc \
    1.23/arg_parser.cc \
    1.23/block.cc \
    1.23/fillbook.cc \
    1.23/genbook.cc \
    1.23/io.cc \
    1.23/loggers.cc \
    1.23/main_common.cc \
    1.23/mapbook.cc \
    1.23/mapfile.cc \
    1.23/non_posix.cc \
    1.23/rational.cc

HEADERS += 1.23/arg_parser.h \
    1.23/block.h \
    1.23/loggers.h \
    1.23/mapbook.h \
    1.23/non_posix.h \
    1.23/rational.h
