#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>
#include "StructureSend.h"
#include "RuntimeSettingsTypes.h"

class CommunicationManager;

/**
 * @file DphPublisher.h
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
     * @brief 配置发送周期
     * @param intervalMs 周期（毫秒）
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
    QString m_endpointId = QStringLiteral("radar_cpu");
    QString m_targetName = QStringLiteral("dph");
    DataPackageHeader m_header{};                    ///< 持久 DPH 状态
    uint64_t m_sendCount = 0;                        ///< 累计发送次数
};
