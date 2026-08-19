#pragma once
/**
 * @file CommTypes.h
 * @brief 定义通信端点的协议无关配置对象。
 * @details 配置由 endpoints.json 解析后交给 CommunicationManager；EndpointConfig 描述本地监听口，
 *          Target 描述逻辑名称到远端地址的映射。业务层只使用 endpointId/targetKey，不直接保存 IP 和端口。
 */

#include <QString>
#include <QMap>

/** @brief 端点传输协议；当前仅 UDP 有具体 Endpoint 实现。 */
enum class Protocol {
    UDP, ///< 无连接数据报，一个 readyRead 事件可能包含多个独立报文。
    TCP  ///< 预留的字节流协议；创建时若无实现应明确失败。
};

/** @brief 一个端点下具名的远端发送目标。 */
struct Target {
    QString address;   ///< IPv4、IPv6 文本地址或可解析主机名。
    quint16 port = 0; ///< 远端 UDP/TCP 端口；0 为无效配置。
};

/** @brief 创建 Endpoint 所需的完整配置快照。 */
struct EndpointConfig {
    QString   id;                          ///< 稳定逻辑 ID，同时用于设备绑定和处理器源映射。
    Protocol  protocol = Protocol::UDP;   ///< 选择具体 Endpoint 子类的传输协议。
    QString   localAddress = QStringLiteral("127.0.0.1"); ///< socket 绑定地址。
    quint16   localPort = 0;               ///< socket 本地监听端口；0 表示由系统选择或配置无效，取决于协议实现。
    QMap<QString, Target> targets;         ///< targetKey 到远端地址的映射，键在单端点内唯一。
    QString   description;                ///< 仅供配置界面和诊断日志展示，不参与路由。
};
