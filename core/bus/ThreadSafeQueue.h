#pragma once
/**
 * @file ThreadSafeQueue.h
 * @brief 提供 SystemBus 和异步存储分支共用的有界多生产者/多消费者队列。
 * @details enqueue() 永不等待容量，用明确失败向上游传递背压；dequeue() 可阻塞等待数据。
 *          shutdown() 是不可逆状态转换，会唤醒所有等待者并使后续入队、出队失败。关闭时队列中尚未消费的
 *          元素不会继续排空，这一语义用于进程退出时尽快终止工作线程。
 */


#include <deque>
#include <QMutex>
#include <QWaitCondition>
#include <QMutexLocker>
#include <atomic>
#include <utility>

/**
 * @brief 基于 QMutex 和 QWaitCondition 的固定容量线程安全队列。
 * @tparam T 队列元素类型；移动出队要求 T 可移动赋值，const 引用入队还要求 T 可复制。
 * @warning 本类型不可复制；销毁前必须确保没有线程仍可能调用其成员函数。
 */
template<typename T>
class ThreadSafeQueue
{
public:
    /**
     * @brief 创建空队列并固定其生命周期容量。
     * @param[in] maxQueueSize 可同时驻留的最大元素数，默认 10000；容量满时 enqueue() 立即失败。
     */
    explicit ThreadSafeQueue(size_t maxQueueSize = 10000)
        : m_maxSize(maxQueueSize)
        , m_shutdown(false)
    {
    }

    /**
     * @brief 将队列置为关闭并唤醒阻塞消费者。
     */
    ~ThreadSafeQueue()
    {
        shutdown();
    }

    /**
     * @brief 复制元素到队尾，不等待空闲容量。
     * @return 成功复制并唤醒一个消费者时为 true；队列已满或已关闭时为 false。
     */
    bool enqueue(const T& item)
    {
        QMutexLocker locker(&m_mutex);

        if (m_shutdown) {
            return false;
        }

        // 检查队列是否已满
        if (m_queue.size() >= m_maxSize) {
            return false;
        }

        m_queue.push_back(item);
        m_condition.wakeOne();
        return true;
    }

    /**
     * @brief 移动元素到队尾，供 unique_ptr 等不可复制类型使用。
     * @return 成功移动并唤醒一个消费者时为 true；队列已满或已关闭时为 false。
     */
    bool enqueue(T&& item)
    {
        QMutexLocker locker(&m_mutex);

        if (m_shutdown) {
            return false;
        }

        // 检查队列是否已满
        if (m_queue.size() >= m_maxSize) {
            return false;
        }

        m_queue.push_back(std::move(item));
        m_condition.wakeOne();
        return true;
    }

    /**
     * @brief 等待并移动取出队首元素。
     * @param[out] item 成功时接收队首元素；失败时保持调用前状态。
     * @param[in] timeoutMs 最大等待毫秒数；负值表示无限等待。
     * @return 取到元素时为 true；超时或 shutdown() 唤醒时为 false。
     * @note 使用 while 重新检查条件，以正确处理虚假唤醒和多消费者竞争。
     */
    bool dequeue(T& item, int timeoutMs = -1)
    {
        QMutexLocker locker(&m_mutex);

        // 等待队列非空或超时
        while (m_queue.empty() && !m_shutdown) {
            if (timeoutMs < 0) {
                m_condition.wait(&m_mutex);
            }
            else {
                if (!m_condition.wait(&m_mutex, timeoutMs)) {
                    return false;
                }
            }
        }

        if (m_shutdown || m_queue.empty()) {
            return false;
        }

        item = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    /**
     * @brief 尝试出队（非阻塞）
     * @param item 输出参数，存储出队的元素
     * @return 是否成功出队
     */
    bool tryDequeue(T& item)
    {
        QMutexLocker locker(&m_mutex);

        if (m_queue.empty() || m_shutdown) {
            return false;
        }

        item = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    /**
     * @brief 检查队列是否为空
     * @return 是否为空
     */
    bool isEmpty() const
    {
        QMutexLocker locker(&m_mutex);
        return m_queue.empty();
    }

    /**
     * @brief 获取队列当前大小
     * @return 队列大小
     */
    size_t size() const
    {
        QMutexLocker locker(&m_mutex);
        return m_queue.size();
    }

    /**
     * @brief 获取队列最大容量
     * @return 最大容量
     */
    size_t maxSize() const
    {
        return m_maxSize;
    }

    /**
     * @brief 清空队列
     */
    void clear()
    {
        QMutexLocker locker(&m_mutex);
        m_queue.clear();
    }

    /**
     * @brief 关闭队列
     * @details 设置关闭标志，唤醒所有等待的线程
     */
    void shutdown()
    {
        QMutexLocker locker(&m_mutex);
        m_shutdown = true;
        m_condition.wakeAll();
    }

    /**
     * @brief 检查队列是否已关闭
     * @return 是否已关闭
     */
    bool isShutdown() const
    {
        QMutexLocker locker(&m_mutex);
        return m_shutdown;
    }

private:
    mutable QMutex m_mutex;       ///< 保护队列内容和关闭状态的唯一互斥量。
    QWaitCondition m_condition;   ///< 入队或关闭时唤醒等待数据的消费者。
    std::deque<T> m_queue;        ///< 先进先出的元素存储。
    size_t m_maxSize;             ///< 构造后不变的容量上限。
    std::atomic<bool> m_shutdown; ///< 不可逆关闭标志；锁外不直接读取。

    // 禁用拷贝构造和赋值
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
};
