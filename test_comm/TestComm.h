/**
 * @file TestComm.h
 * @brief 声明用于端到端验证核心通信链路的雷达模拟器。
 * @details 模拟器监听核心进程发送的 DPH 与工作模式 UDP 报文，并按 CSV 原始节奏回放厂家 GeneralPackage 航迹，
 *          用于验证“工作命令—雷达上报—核心显示”基本链路，而不是生产运行时组件。
 */

#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QDateTime>
#include "StructureSend.h"
#include "StructureReceive.h"

/**
 * @brief 从 CSV 解析并预编码的一条航迹回放记录。
 */
struct CsvTrackRecord {
    QByteArray payload;       ///< 可直接通过 UDP 发送的 GeneralPackage 二进制载荷。
    int deltaMs = 100;        ///< 相对上一条记录的回放间隔，单位毫秒。
};

/**
 * @brief 通信测试工具类（雷达端模拟器）
 * @details 模拟外部雷达设备，用于测试与上位机(core)的 UDP 通信。
 *          支持从 CSV 文件中加载真实雷达数据并回放。
 *
 * 业务流程：
 * - 持续接收 core 每 50ms 发来的 DPH，仅做接收统计。
 * - 接收 core 发来的 workStandby 命令：
 *     Work_Standby_Mode == 1（工作） → 按 CSV 时间戳回放航迹数据；
 *     Work_Standby_Mode == 0（待机） → 停止回放。
 */
class TestComm : public QObject
{
    Q_OBJECT

public:
    /** @brief 构造尚未绑定 UDP 端口的模拟器。 */
    explicit TestComm(QObject* parent = nullptr);
    /** @brief 停止回放计时器并释放由本对象拥有的 UDP 套接字。 */
    ~TestComm() override;

    /**
     * @brief 绑定工作模式和 DPH 接收端口，并连接异步读取回调。
     * @return 两个端口均成功绑定时为 true；任一失败时为 false。
     */
    bool initialize(quint16 workmodePort = 1234, quint16 dphPort = 3456);

    /** @brief 设置模拟航迹发送到核心进程的目标地址和端口。 */
    void setTarget(const QString& address, quint16 port);
    /** @brief 设置未使用 CSV 时间差时的默认航迹发送周期，单位毫秒。 */
    void setTrackReportInterval(int intervalMs);
    /** @brief 保存初始化阶段尝试加载的 CSV 文件路径。 */
    void setCsvPath(const QString& path) { m_csvPath = path; }

    /**
     * @brief 加载 CSV 航迹数据文件
     * @param path CSV 文件路径
     * @return 是否加载成功
     * @details CSV 格式: Time,ID,X,Y,Z,Range,Azimuth(deg),Elevation(deg),
     *          vx,vy,vz,speed,RCS,targetType,AZI_H,ELE_H,Lon,Lat,Hgt,...
     */
    bool loadCsv(const QString& path);

    /** @brief 输出累计 DPH 接收量、场景号和统计速率。 */
    void printDphStats();
    /** @brief 输出工作/待机状态、CSV 加载状态和航迹发送进度。 */
    void printStatus();
    /** @brief 切换逐包打印 DPH 细节的诊断模式。 */
    void toggleVerbose();

private slots:
    void onWorkmodeReadyRead();
    void onDphReadyRead();
    void onTrackReportTick();

private:
    void drainSocket(QUdpSocket* sock,
                     void (TestComm::*handler)(const QByteArray&,
                                               const QHostAddress&, quint16));

    void handleWorkmodePacket(const QByteArray& data,
                              const QHostAddress& sender, quint16 senderPort);
    void handleDphPacket(const QByteArray& data,
                         const QHostAddress& sender, quint16 senderPort);
    void handleWorkStandby(const workStandby& cmd);

    /**
     * @brief 发送指定下标处已预编码的 GeneralPackage，并安排下一条回放间隔。
     * @param index m_csvTracks 中的有效记录下标；越界时停止当前回放。
     */
    void sendTrackFromCsv(int index);

private:
    QUdpSocket* m_socketWorkmode = nullptr;
    QUdpSocket* m_socketDph = nullptr;
    QHostAddress m_targetAddress;
    quint16 m_targetPort = 7788;

    // 航迹回放
    QTimer m_trackTimer;
    int m_trackIntervalMs = 100;
    bool m_working = false;
    uint64_t m_trackSentCount = 0;

    // CSV 数据
    QString m_csvPath;
    QVector<CsvTrackRecord> m_csvTracks;
    int m_csvPlayIndex = 0;
    bool m_csvLoaded = false;

    // DPH 接收统计
    uint64_t m_dphRecvCount = 0;
    uint32_t m_lastSceneNum = 0;
    QElapsedTimer m_statsTimer;
    bool m_verbose = false;
};
