/**
 * @file DeviceConfigService.h
 * @brief 定义 SQLite 设备配置真源到紧凑运行时设备/端点句柄的唯一业务 owner。
 * @details 上游未来由 ApplicationController 提交低频配置命令，本类校验通用安装姿态与端点 JSON，再通过
 * SqliteRuntimeRepository 追加 revision，并向 SqliteArchiveRepository 登记运行时身份。下游通信/协议组合根
 * 只取得已激活句柄；本类不打开 socket、不实例化尚未冻结的 ProtocolAdapter、不发布实时业务数据。
 */

#pragma once

#include "core/config/DeviceConfiguration.h"
#include "core/model/EntityIds.h"

#include <QHash>
#include <QString>
#include <QUuid>
#include <QVector>

namespace core::archive {
class SqliteArchiveRepository;
}

namespace core::persistence {
class SqliteRuntimeRepository;
}

namespace core::model {
class StableIdRegistry;
}

namespace core::config {

/**
 * @brief 持久化、验证并激活设备 revision，同时分配不跨进程保存的紧凑句柄。
 * @warning 所有方法必须在 SQLite 单写线程调用；未来 UI 跨线程请求由 ApplicationController 调度到该线程。
 */
class DeviceConfigService final
{
public:
    /**
     * @brief 绑定低频 SQLite 适配器和归档身份登记入口。
     * @param runtimeRepository 设备 revision 的唯一持久化入口。
     * @param archiveRepository 当前 run 的 CompactId 解析登记入口。
     */
    DeviceConfigService(
        core::persistence::SqliteRuntimeRepository &runtimeRepository,
        core::archive::SqliteArchiveRepository &archiveRepository,
        core::model::StableIdRegistry &stableIds);

    /**
     * @brief 从 SQLite 最新 revision 重建设备与主航迹端点句柄。
     * @return 所有配置均有效并完成绑定返回 true；任一损坏配置使启动失败。
     * @note 只允许在尚未激活任何设备时调用一次。
     */
    bool loadFromDatabase();

    /**
     * @brief 校验并持久化新 revision，再为其分配新的运行时设备和航迹端点句柄。
     * @param configuration 完整设备配置；成功后回填 UUID 与 revision。
     * @param deviceId 成功时写入新激活 DeviceId。
     * @param trackEndpointId 成功时写入 role=track_receive 的 EndpointId。
     * @return SQLite 提交及两类身份绑定全部成功返回 true。
     * @note 保存新 revision 不会改变旧 DeviceId；活动采集仍使用旧 revision，调用者必须显式停机后切换。
     */
    bool saveAndActivate(
        DeviceConfiguration &configuration,
        core::model::DeviceId &deviceId,
        core::model::EndpointId &trackEndpointId);

    /**
     * @brief 从数据库读取当前未删除设备的完整revision快照。
     * @param configurations 成功时替换为稳定UUID排序前的数据库当前集合。
     * @return SQLite查询成功且每条配置均通过当前合同时返回true。
     */
    bool listCurrent(QVector<DeviceConfiguration> &configurations);

    /**
     * @brief 软删除设备并停止其作为新任务选择项出现。
     * @param deviceUuid 稳定设备UUID。
     * @param expectedRevision 前端最后读取的revision。
     * @param changedBy 删除操作者。
     * @param changeReason 删除原因。
     * @return tombstone提交成功返回true；旧运行时句柄仍供已发布事实解析。
     */
    bool softDelete(const QUuid &deviceUuid, quint32 expectedRevision,
                    const QString &changedBy, const QString &changeReason);

    /**
     * @brief 把设备稳定 UUID 解析为当前进程最新激活的设备与主航迹端点句柄。
     * @param deviceUuid SQLite devices 中的稳定设备身份。
     * @param deviceId 成功时写入最新 revision 的运行时句柄。
     * @param trackEndpointId 成功时写入同 revision 的主航迹端点句柄。
     * @return 该 UUID 已从数据库加载或在当前进程保存激活时返回 true。
     */
    bool resolveLatestDevice(
        const QUuid &deviceUuid,
        core::model::DeviceId &deviceId,
        core::model::EndpointId &trackEndpointId) const;

    /**
     * @brief 查询某个已激活 DeviceId 对应的 SQLite revision 主键。
     * @param deviceId 待解析的运行时设备句柄。
     * @param deviceRevisionId 成功时写入正数据库主键。
     * @return 句柄属于本 Service 时返回 true。
     */
    bool resolveDeviceRevision(
        core::model::DeviceId deviceId,
        qint64 &deviceRevisionId) const;

    /** @brief 返回指定运行时设备对应的用户配置revision，用于形成DeviceState。 */
    bool resolveConfigurationRevision(
        core::model::DeviceId deviceId,
        quint32 &configurationRevision) const;

    /** @brief 返回指定运行时设备的不可变配置值副本，供控制Owner在启动采集时冻结。 */
    bool resolveConfiguration(
        core::model::DeviceId deviceId,
        DeviceConfiguration &configuration) const;

    /**
     * @brief 验证设备/端点组合并返回持久 endpointKey。
     * @param deviceId 已激活设备 revision 句柄。
     * @param endpointId 该 revision 分配的主航迹接收端点句柄。
     * @param endpointKey 成功时写入稳定端点键。
     * @return 两个句柄属于同一激活 revision 时返回 true。
     */
    bool resolveTrackEndpoint(
        core::model::DeviceId deviceId,
        core::model::EndpointId endpointId,
        QString &endpointKey) const;

    /**
     * @brief 判断设备 revision 是否具备真正开始采集的最低配置。
     * @return 句柄有效、主航迹端点匹配且 protocolAdapterKey 非空时返回 true。
     * @note 本函数只检查配置就绪，不代表协议实现已经注册或设备能够连接。
     */
    bool isCollectionReady(
        core::model::DeviceId deviceId,
        core::model::EndpointId endpointId) const;

    /**
     * @brief 校验任务请求的设备形态与阵面位图是否属于当前台账版本。
     * @param deviceUuid UI持有的跨重启稳定设备UUID。
     * @param arrayType 任务选择的单面阵或四面阵形态。
     * @param faceMask bit0～bit3表示本次任务启用阵面。
     * @return 设备存在、形态相同且faceMask是已启用阵面的非空子集时返回true。
     */
    bool acceptsTaskBinding(
        const QUuid &deviceUuid,
        RadarArrayType arrayType,
        quint8 faceMask) const;

    /** @brief 返回最近一次配置加载、校验、持久化或绑定失败原因。 */
    QString lastError() const;

private:
    /** @brief 保存一个已激活设备 revision 的数据库主键和主航迹端点。 */
    struct ActiveDevice
    {
        qint64 deviceRevisionId = 0; ///< devices.device_revision_id。
        quint32 configurationRevision = 0; ///< 用户可见且随不可变设备配置递增的revision。
        DeviceConfiguration configuration; ///< 当前revision完整低频配置；不进入实时Topic。
        core::model::EndpointId trackEndpointId; ///< role=track_receive 的进程内句柄。
        QString trackEndpointKey; ///< 对应设备 JSON 中的稳定键。
        QString protocolAdapterKey; ///< 非空才允许 TaskService 启动采集。
        RadarArrayType arrayType = RadarArrayType::SingleFace; ///< 当前revision冻结的整机阵面形态。
        quint8 enabledFaceMask = 0; ///< 当前revision允许任务选择的阵面位图。
    };

    /**
     * @brief 校验通用字段和端点 JSON，并提取唯一主航迹 endpointKey。
     * @param configuration 待保存或从数据库恢复的完整配置。
     * @param trackEndpointKey 成功时写入 role=track_receive 的稳定键。
     * @return 字段范围、JSON 结构和端点唯一性全部成立返回 true。
     */
    bool validateConfiguration(
        const DeviceConfiguration &configuration,
        QString &trackEndpointKey,
        quint8 &enabledFaceMask);

    /** @brief 分配新的非零 DeviceId，耗尽 32 位空间时失败。 */
    core::model::DeviceId allocateDeviceId();

    /** @brief 分配新的非零 EndpointId，耗尽 32 位空间时失败。 */
    core::model::EndpointId allocateEndpointId();

    core::persistence::SqliteRuntimeRepository &m_runtimeRepository; ///< 借用的配置持久化端口。
    core::archive::SqliteArchiveRepository &m_archiveRepository; ///< 借用的归档身份绑定入口。
    core::model::StableIdRegistry &m_stableIds; ///< 借用的跨线程稳定身份注册表；只登记已提交 revision。
    QHash<quint32, ActiveDevice> m_activeDevices; ///< DeviceId 原值到不可变 revision 上下文。
    QHash<QUuid, core::model::DeviceId> m_latestDeviceIds; ///< 稳定 UUID 到最新激活句柄。
    quint32 m_nextDeviceId = 1; ///< 下一个候选 DeviceId；零永不分配。
    quint32 m_nextEndpointId = 1; ///< 下一个候选 EndpointId；零永不分配。
    QString m_lastError; ///< 最近失败原因，不进入实时链。
};

} // namespace core::config
