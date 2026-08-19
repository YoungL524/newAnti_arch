/**
 * @file DeviceRegistry.h
 * @brief 定义设备台账、雷达六自由度安装信息及端点唯一绑定规则。
 * @details 台账持久化业务设备而不是网络 Endpoint；雷达通过 endpointId 绑定接收入口，热路径缓存将报文
 *          归属到 deviceId。每个通信端点最多绑定一台雷达，写入新绑定时会迁移旧绑定并报告受影响设备。
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantMap>
#include <QVector>
#include <optional>

class DataStore;

/**
 * @brief 设备业务类别，决定配置表单、绑定约束和任务能力。
 */
enum class DeviceKind : int {
    Radar = 0, ///< 雷达及其独立阵面配置。
    Opto = 1,  ///< 光电传感器。
    Level = 2, ///< 调平或姿态辅助设备。
    Gps = 3,   ///< 卫星定位设备。
};

/**
 * @brief 面向操作员的设备生命周期状态。
 */
enum class DeviceStatus : int {
    Offline = 0, ///< 不可用或尚未确认就绪。
    Standby = 1, ///< 可达但未执行任务。
    Working = 2, ///< 正在执行任务。
    Fault = 3,   ///< 已报告需要处理的故障。
};

/**
 * @brief 数据库和 Web API 交换的完整业务设备记录。
 */
struct DeviceInfo {
    QString id;                    ///< 稳定唯一业务 ID。
    QString name;                  ///< 操作员可见名称。
    QString brand;                 ///< 厂家或型号标签。
    DeviceKind kind = DeviceKind::Radar; ///< 设备类别。
    double lng = 0;                ///< coordinateSystem 下的经度，十进制度。
    double lat = 0;                ///< coordinateSystem 下的纬度，十进制度。
    double altitude = 0;           ///< 安装高程，米。
    double pitch = 0;              ///< 绝对安装俯仰角，度。
    double roll = 0;               ///< 绝对安装横滚角，度。
    double maxRange = 0;           ///< 配置最大探测距离，千米。
    QString facesJson = QStringLiteral("[]"); ///< 阵面 JSON；每个阵面自身保存以正北为 0、顺时针为正的绝对朝向。
    QString endpointId;            ///< 绑定通信端点；未绑定时为空。
    QString linkedRadarId;         ///< 非雷达设备所属的父雷达 ID。
    double mountOffsetAzimuth = 0; ///< 为旧数据库兼容保留的安装偏移字段，不参与阵面绝对朝向换算。
    QString coordinateSystem = QStringLiteral("WGS-84"); ///< 经纬高空间参考。
    DeviceStatus status = DeviceStatus::Offline; ///< 持久化设备状态。
    qint64 createdAt = 0;           ///< Unix 毫秒创建时间。
    qint64 updatedAt = 0;           ///< Unix 毫秒更新时间。

    /**
 * @brief 转换为 DataStore 插入/更新和 JSON 适配使用的键值记录。
 */
    QVariantMap toVariantMap() const;
    /**
 * @brief 从数据库查询行恢复设备记录，并对缺失的兼容字段应用默认值。
 */
    static DeviceInfo fromVariantMap(const QVariantMap& m);
};

/**
 * @brief 设备 CRUD、端点唯一性和端点到设备热路径缓存的拥有者。
 */
class DeviceRegistry {
public:
    /**
 * @brief 使用非拥有 DataStore 创建台账；DataStore 必须覆盖本对象生命周期。
 */
    explicit DeviceRegistry(DataStore* store);

    /**
 * @brief 创建或迁移 devices 表及端点绑定索引。
 */
    bool ensureSchema();
    /// 为指定设备类型生成稳定唯一的业务 id
    static QString generateId(DeviceKind kind);
    /// 确保至少有一条绑定 radar_cpu 端点的可控雷达业务设备。
    /// 新库默认创建 id=radar_001, endpointId=radar_cpu。
    /// 历史库可能存在 id=radar_cpu 的 legacy 设备。
    bool ensureDefaultControllableRadar();
    /**
 * @brief 返回全部设备记录的持久化快照。
 */
    QVector<DeviceInfo> list() const;
    /**
 * @brief 按稳定 ID 查询一台设备。
 * @return 存在时返回记录；不存在或查询失败时为空。
 */
    std::optional<DeviceInfo> get(const QString& id) const;
    /// 按 endpointId 查找绑定的 Radar 业务设备（用于航迹归属；多个时返回空，避免错误映射）
    std::optional<DeviceInfo> findRadarByEndpointId(const QString& endpointId) const;
    /// 热路径：endpointId → 业务 deviceId（内存缓存，无 SQLite）
    QString cachedDeviceIdForEndpoint(const QString& endpointId) const;
    /**
 * @brief 从数据库重建唯一的 endpointId 到雷达 deviceId 映射；冲突端点不进入缓存。
 */
    void refreshEndpointCache();
    /**
 * @brief 校验字段和端点唯一性后持久化具有显式 ID 的设备。
 */
    bool create(const DeviceInfo& dev);
    /// 创建设备，id 为空时自动生成（含冲突重试）
    std::optional<DeviceInfo> createAssigningId(DeviceInfo dev);
    /**
 * @brief 更新设备；雷达抢占端点绑定时在同一事务解除旧设备绑定。
 */
    bool update(const DeviceInfo& dev);
    /**
 * @brief 删除设备并刷新端点缓存。
 * @return 数据库删除成功时返回 true。
 */
    bool remove(const QString& id);
    /**
 * @brief 返回最近一次失败的操作员可见诊断文本。
 */
    QString lastError() const { return m_lastError; }
    /// 最近一次 create/update 因 endpoint 绑定迁移而解除绑定的设备 id（取后清空）
    QStringList takeLastMigratedDeviceIds();

private:
    /// 查询所有绑定该 endpoint 的 Radar 设备（写端冲突检查用）
    QVector<DeviceInfo> findRadarDevicesByEndpoint(const QString& endpointId) const;
    static bool validateDeviceWrite(const DeviceInfo& dev);
    DataStore* m_store = nullptr;                   ///< 非拥有持久化服务。
    QHash<QString, QString> m_endpointToDeviceId;  ///< 无 SQLite 的接收热路径映射。
    QStringList m_lastMigratedDeviceIds;           ///< 最近写操作解除绑定的设备 ID，一次读取后清空。
    QString m_lastError;                            ///< 最近失败诊断。
};
