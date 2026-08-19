#pragma once

/**
 * @file DphPublisher.h
 * @brief 声明独立于航迹事件的固定周期 DataPackageHeader 发送器。
 * @details DPH 是雷达协议要求的 50 ms 周期大包头，不适合由“有数据才触发”的 SystemBus 链路驱动。
 *          本类在 QObject 线程使用 QTimer 维护连续景幅号和运行参数快照，经 CommunicationManager
 *          直接发送；发送失败不停止定时器，下个周期会继续尝试。sendCount 统计发送尝试次数而非成功次数。
 */

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>
#include "StructureSend.h"
#include "RuntimeSettingsTypes.h"

class CommunicationManager;

/**
 * @class DphPublisher
 * @brief DPH 定时发送器
 * @details 负责独立维护 `DataPackageHeader` 状态，并按固定周期通过
 *          `CommunicationManager` 发送到指定端点目标。
 *
 * 职责边界：
 * - 管理 DPH 的周期发送定时器
 * - 维护景幅号、自增发送计数等持续状态
 * - 将组装后的 `DataPackageHeader` 发送到通信层
 *
 * 非职责：
 * - 不负责业务命令路由
 * - 不直接访问 `SystemBus`
 * - 不管理 `CommunicationManager` 生命周期
 */
class DphPublisher : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param commManager 通信管理器（非持有，生命周期需覆盖本对象）
     * @param parent Qt parent
     */
    explicit DphPublisher(CommunicationManager* commManager, QObject* parent = nullptr);

    ~DphPublisher() override;

    /**
     * @brief 配置发送目标
     * @param endpointId 端点 ID
     * @param targetName 端点下的目标名
     */
    void setRoute(const QString& endpointId, const QString& targetName);

    /**
     * @brief 配置 QTimer 周期。
     * @param[in] intervalMs 周期毫秒数；应为正值，生产默认值为 50 ms。
     */
    void setInterval(int intervalMs);

    /**
     * @brief 启动定时发送
     */
    void start();

    /**
     * @brief 停止定时发送
     */
    void stop();

    /**
     * @brief 获取累计发送次数
     */
    uint64_t sendCount() const { return m_sendCount; }

    /**
     * @brief 获取当前景幅号
     */
    uint32_t sceneNumber() const { return m_header.F_UI_Scene_Num; }

    /**
     * @brief 应用运行时参数（只更新映射字段，不重置 DPH 持续状态）
     * @param settings 运行时参数
     */
    void applyRuntimeSettings(const RadarRuntimeSettings& settings);

private slots:
    /**
     * @brief 定时发送一次 DPH
     */
    void sendOnce();

private:
    CommunicationManager* m_commManager = nullptr;  ///< 通信管理器（非持有）
    QTimer m_timer;                                  ///< 周期发送定时器
    QString m_endpointId = QStringLiteral("radar_cpu"); ///< DPH 所使用的通信端点 ID。
    QString m_targetName = QStringLiteral("dph");       ///< 端点下的具名 UDP 目标。
    DataPackageHeader m_header{};                    ///< 跨发送周期保留的协议头与景幅号。
    uint64_t m_sendCount = 0;                        ///< 定时器触发并尝试发送的累计次数，包含失败尝试。
};
