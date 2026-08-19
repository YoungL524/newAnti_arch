/**
 * @file AppTypes.h
 * @brief 定义 AppController 向启动入口和 Web 状态接口暴露的轻量结果对象。
 * @details 这里只放跨应用边界传递的只读快照，不放服务对象或业务行为。所有时间戳使用 Unix 毫秒，
 *          计数器均为进程启动后的累计值；读取方不得把一次快照当作持续同步的状态引用。
 */

#pragma once

#include <QtGlobal>
#include <QHash>
#include <QString>
#include <cstdint>
#include "LatencyStats.h"

/**
 * @brief Web 服务监听结果，由 AppController::startWebService() 返回。
 */
struct WebServiceResult {
    bool ok = false;       ///< HTTP 与 WebSocket 两个监听器均成功启动时为 true。
    quint16 httpPort = 0;  ///< 实际 HTTP 监听端口；启动失败时为 0。
    quint16 wsPort = 0;    ///< 实际 WebSocket 监听端口；启动失败时为 0。
};

/**
 * @brief 某一采样时刻的系统健康快照，供诊断接口展示而非故障判定状态机使用。
 */
struct AppHealth {
    struct SystemBusStatus {
        bool running = false;       ///< 总线工作线程是否仍接受并分发消息。
        int subscriptionCount = 0; ///< 采样时已登记的订阅数量。
    };
    struct CommStatus {
        int totalEndpoints = 0; ///< 已配置的通信端点总数。
        int openEndpoints = 0;  ///< 采样时 socket 已打开的端点数。
        /// 各 endpoint 是否已打开（healthReport 一次性采样）
        QHash<QString, bool> endpointOpen;
    };
    struct ProcessingStatus {
        uint64_t generalPackageCount = 0; ///< 厂家总包成功解析的累计数量。
        uint64_t trackInsertCount = 0;    ///< TrackStorage 成功持久化的累计航迹数量。
        uint64_t routedCount = 0;         ///< 反向总线消息成功路由到通信层的累计数量。
    };
    struct DatabaseStatus {
        bool configured = false; ///< AppController 是否创建了 DataStore。
        bool connected = false;  ///< 当前数据库连接是否可用。
    };

    SystemBusStatus systemBus;
    CommStatus comm;
    ProcessingStatus processing;
    DatabaseStatus database;
    LatencyStats uplinkLatency;
    LatencyStats downlinkLatency;
    qint64 timestampMs = 0; ///< 生成快照的 Unix 时间戳，单位毫秒。
};
