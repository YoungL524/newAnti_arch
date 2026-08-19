/**
 * @file TrackTypes.h
 * @brief 定义权威航迹、显示投影和交互命令三类稳定业务数据契约。
 * @details TrackPacket 是识别、存储和插件消费的不可变事实；TrackDisplayPoint/Update 是从事实派生的
 *          有界显示窗口；TrackInteractionCommand 是 UI 反向输入。显示样式和选择状态不得写回权威航迹。
 */

#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

/** @brief 航迹从协议映射到可发布事实的识别阶段状态机。 */
enum class RecognitionStatus : uint8_t {
    Pending = 0, ///< 已标准化，等待识别器处理，不能发布为最终事实。
    Completed,   ///< 识别阶段完成；类型仍可为 Unknown。
    Failed       ///< 识别阶段失败，不应进入权威总线链路。
};

enum class RecognitionType : uint8_t {
    Unknown = 0
};

/**
 * @brief 航迹主干唯一流转模型
 * @details 适配器创建，识别模块原地完成，识别完成后以只读共享指针发布。
 *          不包含厂家协议结构、固定 rawData、显示样式或交互状态。
 */
struct TrackPacket {
    uint16_t schemaVersion = 1; ///< 模型字段版本，持久化/插件升级时用于兼容判断。
    uint64_t traceId = 0;       ///< 单进程链路追踪 ID，不作为跨重启业务主键。

    QString endpointId; ///< 原始报文入口端点。
    QString deviceId;   ///< 设备台账中的业务设备 ID。
    QString taskId;     ///< 接收时所属活动任务；为空时存储分支会跳过。

    qint64 receivedAtMs = 0;       ///< UDP 读取后的 Unix 毫秒业务时间。
    qint64 ingressSteadyNs = 0;    ///< UDP 入口单调纳秒，仅可在进程内做差。
    qint64 normalizedSteadyNs = 0; ///< 标准化映射完成的单调纳秒。
    qint64 recognizedSteadyNs = 0; ///< 识别阶段完成的单调纳秒。
    uint32_t sceneNumber = 0;      ///< 厂家景幅号，用于扫描轮次关联。

    uint32_t trackId = 0;       ///< 厂家在单设备范围内分配的航迹号。
    double rangeM = 0;          ///< 雷达斜距，单位米。
    double azimuthRad = 0;      ///< 雷达局部方位角，单位弧度。
    double elevationRad = 0;    ///< 雷达局部俯仰角，单位弧度。
    double radialSpeedMps = 0;  ///< 径向速度，单位米/秒。
    double rcs = 0;             ///< 雷达散射截面积，沿用厂家数值口径。
    double altitudeM = 0;       ///< 目标高度，单位米。
    double xM = 0;              ///< 雷达局部 X 坐标，单位米。
    double yM = 0;              ///< 雷达局部 Y 坐标，单位米。
    double zM = 0;              ///< 雷达局部 Z 坐标，单位米。
    double vxMps = 0;           ///< 局部 X 轴速度，单位米/秒。
    double vyMps = 0;           ///< 局部 Y 轴速度，单位米/秒。
    double vzMps = 0;           ///< 局部 Z 轴速度，单位米/秒。

    RecognitionStatus recognitionStatus = RecognitionStatus::Pending;
    RecognitionType recognitionType = RecognitionType::Unknown;
};

/**
 *  @brief 复合 key（内部 interaction map 及缓存索引）
 */

struct TrackKey {
    QString   deviceId;
    uint32_t  trackId = 0;

    bool operator==(const TrackKey& o) const {
        return deviceId == o.deviceId && trackId == o.trackId;
    }
};

inline uint qHash(const TrackKey& k, uint seed = 0) {
    return qHash(k.deviceId, seed) ^ qHash(k.trackId);
}

/**
 * @brief Web 显示窗口中的单个航迹点，是 TrackPacket 的可丢弃派生投影。
 */
struct TrackDisplayPoint {
    uint32_t  trackId = 0;
    QString   deviceId;
    QString   taskId;            // 归属任务（未归属时为空）
    qint64    timestamp = 0;    ///< Unix 毫秒。
    double    range = 0;        ///< 米。
    double    azimuth = 0;      ///< 弧度。
    double    elevation = 0;    ///< 弧度。
    double    altitude = 0;     ///< 米。
    double    x = 0, y = 0, z = 0; ///< 雷达局部坐标，米。
    double    radialSpeed = 0;  ///< 米/秒。
    double    rcs = 0;
    uint32_t  pointIndex = 0;    // 绝对递增序号

    // 显示状态
    struct Style {
        QString color  = QStringLiteral("#22c55e");
        double  size   = 3.0;
        double  opacity = 0.85;
    } style;

    struct Interaction {
        bool selected = false;
        bool trackingEnabled = false;
    } interaction;
};

struct TrackDisplayPointKey {
    QString deviceId;
    uint32_t trackId = 0;
    uint32_t pointIndex = 0;

    bool operator==(const TrackDisplayPointKey& other) const {
        return deviceId == other.deviceId
            && trackId == other.trackId
            && pointIndex == other.pointIndex;
    }
};

inline uint qHash(const TrackDisplayPointKey& key, uint seed = 0) {
    seed = qHash(key.deviceId, seed);
    seed ^= qHash(key.trackId, seed << 1);
    return seed ^ qHash(key.pointIndex, seed << 1);
}

enum class TrackDisplayUpdateMode : uint8_t {
    Snapshot = 0, ///< 完整替换客户端当前窗口，用于首次连接、任务切换或修订恢复。
    Delta         ///< 仅应用 upserts/removals，要求 revision 与客户端连续。
};

/**
 * @brief 航迹显示同步事件。
 * @details Snapshot 用于首次同步/任务切换，Delta 只携带变化点和删除键。
 */
struct TrackDisplayUpdate {
    TrackDisplayUpdateMode mode = TrackDisplayUpdateMode::Delta;
    uint64_t revision = 0; ///< 单调递增显示流修订号；跳号时客户端必须请求快照。
    qint64 timestamp = 0;  ///< 构建更新的 Unix 毫秒。
    qint64 buildStartedSteadyNs = 0;
    qint64 builtSteadyNs = 0;     // 仅用于 core 内部链路追踪
    qint64 publishedSteadyNs = 0;
    qint64 latestIngressSteadyNs = 0;
    qint64 latestDisplayReadySteadyNs = 0;
    QVector<TrackDisplayPoint> upserts;    ///< 新增或字段发生变化的显示点。
    QVector<TrackDisplayPointKey> removals; ///< 客户端必须删除的窗口点键。
};


/**
 * @brief UI 航迹交互命令（WebApiController 发布，TrackDisplayManager 订阅）
 */
struct TrackInteractionCommand {
    QString taskId;
    QString deviceId;
    uint32_t trackId = 0;
    bool selected = false;
    bool trackingEnabled = false;
    qint64 timestamp = 0;
};
