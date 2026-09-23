/**
 * @file LatestState.h
 * @brief 实现单一强类型完整状态的 KeepLast(1) 共享存储。
 * @details 上游唯一状态 owner 在完成本地状态转换后调用 set；下游 WebReader、监控 Reader 等
 * Consumer 通过 latest() 主动读取最新完整状态与 revision。该类不保存 Consumer revision、不合并局部
 * patch、不执行状态机或 Command dispatch；新人应从 set() 的原子替换与解锁后 Doorbell 顺序开始阅读。
 */

#pragma once

#include "core/datadomain/TopicDoorbell.h"
#include "core/datadomain/TopicTypes.h"

#include <QMutex>
#include <QMutexLocker>

#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace core::domain {

/**
 * @brief 只保留一个不可变完整状态，并为每次成功替换分配单调 revision。
 * @tparam T 强类型完整状态模型，不得使用局部更新载荷实例化。
 * @details Reader 取得 shared_ptr 后，后续 set 可以替换 Topic 持有的最新值而不使已有只读句柄失效。
 * @warning 对象由组合根持有；销毁时不得仍有 set/latest 调用，且应在 Doorbell 的创建线程销毁。
 */
template<typename T>
class LatestState final
{
public:
    /** @brief 创建 revision 为零、尚未设置任何完整状态的共享对象。 */
    LatestState() = default;

    /** @brief 禁止复制包含互斥量和 QObject Doorbell 的状态对象。 */
    LatestState(const LatestState&) = delete;

    /** @brief 禁止复制赋值，防止两个状态对象错误共享 owner 语义。 */
    LatestState& operator=(const LatestState&) = delete;

    /** @brief 禁止移动，保证 Consumer 持有的 Doorbell 地址与线程亲和性稳定。 */
    LatestState(LatestState&&) = delete;

    /** @brief 禁止移动赋值，避免运行中的 Doorbell 连接指向失效对象。 */
    LatestState& operator=(LatestState&&) = delete;

    /**
     * @brief 原子替换当前完整状态并递增 revision。
     * @param sample 唯一状态 owner 已经计算完成的完整状态；函数取得其所有权。
     * @return 本次替换分配的 revision，从 1 开始单调递增。
     * @throws std::overflow_error revision 即将耗尽时抛出，旧状态保持不变且不触发 Doorbell。
     * @note shared_ptr 在锁外分配；锁内只交换句柄和 revision；旧状态在锁外析构，随后才触发 Doorbell，
     * 避免复杂状态载荷的析构阻塞其他 Reader。
     */
    StateRevision set(core::model::Sample<T> sample)
    {
        auto sharedSample = std::make_shared<const core::model::Sample<T>>(std::move(sample));
        SamplePtr<T> replacedSample;
        StateRevision newRevision = 0;

        {
            QMutexLocker locker(&m_mutex);
            if (m_revision == std::numeric_limits<StateRevision>::max()) {
                throw std::overflow_error("LatestState revision exhausted");
            }
            newRevision = ++m_revision;
            replacedSample = std::move(m_sample);
            m_sample = std::move(sharedSample);
        }

        replacedSample.reset();
        m_doorbell.ring(newRevision);
        return newRevision;
    }

    /**
     * @brief 返回同一次短锁快照中的当前完整状态与 revision。
     * @return 尚未 set 时返回 std::nullopt，否则返回不可变状态句柄及其对应 revision。
     * @note 返回后 owner 可以继续 set；Reader 已取得的旧句柄在释放前仍然有效。
     */
    std::optional<StateRecord<T>> latest() const
    {
        QMutexLocker locker(&m_mutex);
        if (!m_sample) {
            return std::nullopt;
        }
        return StateRecord<T> {m_revision, m_sample};
    }

    /**
     * @brief 返回读取瞬间的最新 revision。
     * @return 尚未 set 时为零；否则与当时 latest() 中记录的 revision 相同。
     * @note 若调用者同时需要状态和 revision，必须调用 latest() 获取一致快照，不能分别读取后假设一致。
     */
    StateRevision revision() const
    {
        QMutexLocker locker(&m_mutex);
        return m_revision;
    }

    /**
     * @brief 返回仅通知 revision 水位的非拥有型 Doorbell 指针。
     * @return 指针在 LatestState 生命周期内有效；信号不携带 T，也不能代替 latest()。
     */
    const TopicDoorbell* doorbell() const noexcept
    {
        return &m_doorbell;
    }

private:
    mutable QMutex m_mutex; ///< 只保护当前 Sample 句柄和 revision 的短临界区。
    SamplePtr<T> m_sample;  ///< 唯一由 LatestState 长期持有的最新完整状态；未设置时为空。
    StateRevision m_revision = 0; ///< 每次成功替换递增；零专用于“尚无状态”。
    TopicDoorbell m_doorbell; ///< 解锁后触发的 revision 提示，不保存状态本体。
};

} // namespace core::domain
