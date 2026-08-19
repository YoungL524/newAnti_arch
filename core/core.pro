QT += core network sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = NewAntiDrone_core
TEMPLATE = app

INCLUDEPATH += \
    $$PWD/comm \
    $$PWD/protocol \
    $$PWD/bus \
    $$PWD/processor \
    $$PWD/data \
    $$PWD/business

HEADERS += \
    comm/CommTypes.h \
    comm/Endpoint.h \
    comm/UdpEndpoint.h \
    comm/CommunicationManager.h \
    comm/EndpointConfigLoader.h \
    protocol/StructureSend.h \
    protocol/StructureReceive.h \
    bus/ThreadSafeQueue.h \
    bus/GenericPackets.h \
    bus/ISystemBus.h \
    bus/SystemBus.h \
    processor/RadarDataProcessor.h \
    processor/BusToCommRouter.h \
    processor/DphPublisher.h \
    data/ConfigManager.h \
    data/DataStore.h \
    data/TrackStorage.h \
    business/TrackTypes.h \
    AppController.h \
    StdinReader.h

SOURCES += \
    main.cpp \
    comm/Endpoint.cpp \
    comm/UdpEndpoint.cpp \
    comm/CommunicationManager.cpp \
    comm/EndpointConfigLoader.cpp \
    bus/SystemBus.cpp \
    processor/RadarDataProcessor.cpp \
    processor/BusToCommRouter.cpp \
    processor/DphPublisher.cpp \
    data/ConfigManager.cpp \
    data/DataStore.cpp \
    data/TrackStorage.cpp \
    AppController.cpp
