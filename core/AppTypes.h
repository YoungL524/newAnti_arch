#pragma once

#include <QtGlobal>
#include <cstdint>
#include "LatencyStats.h"

/**
 * @brief Web 服务启动结果
 */
struct WebServiceResult {
    bool ok = false;
    quint16 httpPort = 0;
    quint16 wsPort = 0;
};

/**
 * @brief 系统健康报告
 */
struct AppHealth {
    struct SystemBusStatus {
        bool running = false;
        int subscriptionCount = 0;
    };
    struct CommStatus {
        int totalEndpoints = 0;
        int openEndpoints = 0;
    };
    struct ProcessingStatus {
        uint64_t generalPackageCount = 0;
        uint64_t trackInsertCount = 0;
        uint64_t routedCount = 0;
    };
    struct DatabaseStatus {
        bool configured = false;
        bool connected = false;
    };

    SystemBusStatus systemBus;
    CommStatus comm;
    ProcessingStatus processing;
    DatabaseStatus database;
    LatencyStats uplinkLatency;
    LatencyStats downlinkLatency;
    qint64 timestampMs = 0;
};
