/**
 * @file HeartbeatStateOwner.h
 * @brief 接收Adapter已校验心跳，形成DeviceState完整快照。
 * @details 从accept阅读；唯一owner与通信同线程，定时器只检查心跳期限，不轮询Topic。
 * 不拥有Socket、不执行命令、不推送Web数据。后续设备控制必须通过此唯一状态owner合并。
 */
#pragma once
#include "core/datadomain/LatestState.h"
#include "core/model/RuntimeState.h"
#include <QObject>
#include <QTimer>
#include <array>
namespace core::producer {
/** @brief 同线程心跳状态机，借用State必须比owner存活更久。 */
class HeartbeatStateOwner final : public QObject {
public:
    /** @brief 初始化灰色完整快照，不启动期限检查；组合根须显式调用start。 */
    explicit HeartbeatStateOwner(core::domain::LatestState<core::model::DeviceState>& state);
    /** @brief 清除旧接收期限和心跳状态，网络启动成功后启用检查。 */
    void start();
    /** @brief 停止期限检查；组合根应先停止接收。 */
    void stop();
    /** @brief 接收在线值及位图，sp区分来源；UTC毫秒用于记录，steady纳秒用于期限；无效输入忽略。 */
    void accept(bool sp, quint16 online, quint16 mask, qint64 utcMs, qint64 steadyNs);
    /** @brief 按单调时间检查2秒迟到及5秒超时；显式时钟参数用于确定性边界测试。 */
    void check(qint64 utcMs, qint64 steadyNs);
private:
    /** @brief 为完整快照填充时间，门禁通过后才替换State；仅在状态变化时调用。 */
    void publish(core::model::Sample<core::model::DeviceState> sample, qint64 utc, qint64 steady);
    core::domain::LatestState<core::model::DeviceState>& m_state; ///< 借用的唯一状态本体。
    std::array<qint64, 2> m_last{}; ///< DP/SP最近有效接收单调纳秒，零表示尚无心跳。
    QTimer m_timer; ///< owner期限检查，随QObject线程移动；不保存业务消息。
};
}
