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
 * @brief 从 CSV 解析的一条航迹记录（已序列化为 UDP 载荷 + 时间偏移）
 */
struct CsvTrackRecord {
    QByteArray payload;       ///< 预序列化的 GeneralPackage
    int deltaMs = 100;        ///< 距上一条的时间偏移 (ms)
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
    explicit TestComm(QObject* parent = nullptr);
    ~TestComm() override;

    bool initialize(quint16 workmodePort = 1234, quint16 dphPort = 3456);

    void setTarget(const QString& address, quint16 port);
    void setTrackReportInterval(int intervalMs);
    void setCsvPath(const QString& path) { m_csvPath = path; }

    /**
     * @brief 加载 CSV 航迹数据文件
     * @param path CSV 文件路径
     * @return 是否加载成功
     * @details CSV 格式: Time,ID,X,Y,Z,Range,Azimuth(deg),Elevation(deg),
     *          vx,vy,vz,speed,RCS,targetType,AZI_H,ELE_H,Lon,Lat,Hgt,...
     */
    bool loadCsv(const QString& path);

    void printDphStats();
    void printStatus();
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
     * @brief 从 CSV 数据构造一条 GeneralPackage 并发送
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
