#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QTimer>
#include "StructureSend.h"
#include "StructureReceive.h"

/**
 * @brief 通信测试工具类（雷达端模拟器）
 * @details 模拟外部雷达设备，用于测试与上位机(core)的 UDP 通信
 *
 * 业务流程（反应式）：
 * - 持续接收 core 每 50ms 发来的 DPH，仅做接收统计，不回发。
 * - 接收 core 发来的 workStandby 命令：
 *     Work_Standby_Mode == 1（工作） → 启动航迹上报定时器，周期性向 core 发送 GeneralPackage；
 *     Work_Standby_Mode == 0（待机） → 停止航迹上报定时器。
 * - 仅在"工作"状态下上报航迹，"待机"状态下不上报；DPH 接收与工作状态无关。
 */
class TestComm : public QObject
{
    Q_OBJECT

public:
    explicit TestComm(QObject* parent = nullptr);
    ~TestComm() override;

    /**
     * @brief 初始化 UDP 套接字（双端口监听）
     * @param workmodePort 监听 workmode 的端口（默认 1234）
     * @param dphPort 监听 DPH 的端口（默认 3456）
     * @return 是否初始化成功
     * @details 本端模拟雷达硬件：workmode 和 DPH 走独立通道，便于后续扩展
     *          每种命令可独立抓包/替换为真实硬件。上报航迹时复用 workmode socket
     *          发送（因为 workmode 与航迹属于同一命令-响应链路）。
     */
    bool initialize(quint16 workmodePort = 1234, quint16 dphPort = 3456);

    /**
     * @brief 设置目标地址（上位机 core 地址）
     * @param address 目标 IP 地址
     * @param port 目标端口
     */
    void setTarget(const QString& address, quint16 port);

    /**
     * @brief 设置航迹上报周期
     * @param intervalMs 上报间隔（毫秒），默认 100ms
     */
    void setTrackReportInterval(int intervalMs);

    // ========== 查询接口 ==========

    /**
     * @brief 打印 DPH 接收统计信息
     */
    void printDphStats();

    /**
     * @brief 打印当前工作/上报状态
     */
    void printStatus();

    /**
     * @brief 切换 DPH 详细打印模式
     * @details 默认关闭（每 50ms 一个包，打印太多会刷屏）
     */
    void toggleVerbose();

private slots:
    /**
     * @brief workmode 端口（1234）收到数据
     */
    void onWorkmodeReadyRead();

    /**
     * @brief DPH 端口（3456）收到数据
     */
    void onDphReadyRead();

    /**
     * @brief 航迹上报定时器触发
     * @details 构造一个 GeneralPackage（含模拟航迹）发送到 core
     */
    void onTrackReportTick();

private:
    /**
     * @brief 从指定 socket 读空所有 pending 数据报并分发给 handler
     */
    void drainSocket(QUdpSocket* sock,
                     void (TestComm::*handler)(const QByteArray&,
                                               const QHostAddress&, quint16));

    /**
     * @brief 处理 workmode 数据包
     */
    void handleWorkmodePacket(const QByteArray& data,
                              const QHostAddress& sender, quint16 senderPort);

    /**
     * @brief 处理 DPH 数据包
     */
    void handleDphPacket(const QByteArray& data,
                         const QHostAddress& sender, quint16 senderPort);

    /**
     * @brief 处理 workStandby 命令：根据模式启停上报定时器
     */
    void handleWorkStandby(const workStandby& cmd);

private:
    QUdpSocket* m_socketWorkmode = nullptr;  ///< 绑定 1234，接收 workmode 命令 + 上报航迹
    QUdpSocket* m_socketDph = nullptr;       ///< 绑定 3456，只接收 DPH
    QHostAddress m_targetAddress;            ///< 目标地址（core）
    quint16 m_targetPort = 7788;             ///< 目标端口（core 监听端口）

    // 航迹上报
    QTimer m_trackTimer;                     ///< 航迹上报定时器
    int m_trackIntervalMs = 100;             ///< 上报周期（毫秒）
    bool m_working = false;                  ///< 当前是否处于工作模式
    uint32_t m_nextTargetId = 1;             ///< 自增目标 ID
    uint64_t m_trackSentCount = 0;           ///< 已上报航迹次数

    // DPH 接收统计
    uint64_t m_dphRecvCount = 0;             ///< DPH 接收总数
    uint32_t m_lastSceneNum = 0;             ///< 上一次收到的景幅序号
    QElapsedTimer m_statsTimer;              ///< 统计计时器
    bool m_verbose = false;                  ///< 是否详细打印每个 DPH
};
