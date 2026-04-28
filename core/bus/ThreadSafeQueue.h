#pragma once

#include <deque>
#include <QMutex>
#include <QWaitCondition>
#include <QMutexLocker>
#include <atomic>
#include <utility>

/**
 * @brief 线程安全队列模板类
 * @details 支持多线程环境下的安全数据传输
 *          提供阻塞和非阻塞的入队出队操作
 */
template<typename T>
class ThreadSafeQueue
{
public:
    /**
     * @brief 构造函数
     * @param maxQueueSize 队列最大容量，默认10000
     */
    explicit ThreadSafeQueue(size_t maxQueueSize = 10000)
        : m_maxSize(maxQueueSize)
        , m_shutdown(false)
    {
    }

    /**
     * @brief 析构函数
     */
    ~ThreadSafeQueue()
    {
        shutdown();
    }

    /**
     * @brief 入队操作（非阻塞）
     * @param item 要入队的元素
     * @return 是否成功入队
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
     * @brief 入队操作（非阻塞，移动语义）
     * @param item 要入队的元素（右值引用）
     * @return 是否成功入队
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
     * @brief 出队操作（阻塞）
     * @param item 输出参数，存储出队的元素
     * @param timeoutMs 超时时间（毫秒），-1表示无限等待
     * @return 是否成功出队
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
    mutable QMutex m_mutex;           // 互斥锁
    QWaitCondition m_condition;       // 条件变量
    std::deque<T> m_queue;           // 底层队列
    size_t m_maxSize;                // 最大容量
    std::atomic<bool> m_shutdown;    // 关闭标志

    // 禁用拷贝构造和赋值
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
};
