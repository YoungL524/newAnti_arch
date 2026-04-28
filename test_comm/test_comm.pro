QT += core network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = test_comm
TEMPLATE = app

INCLUDEPATH += \
    ../core/protocol \
    ../core/comm

HEADERS += \
    ../core/protocol/StructureSend.h \
    ../core/protocol/StructureReceive.h \
    TestComm.h \
    StdinReader.h

SOURCES += \
    main.cpp \
    TestComm.cpp
