/**
 * @file DeviceRegistryHttpService.h
 * @brief 定义设备台账与任务登记使用的最小HTTP入口。
 * @details 上游 React 调用设备及任务 HTTP；下游通过有界异步调用访问 StorageRuntime，采集编排交给 ApplicationController。
 * 本类只拥有 Socket、请求容量及写请求互斥，不访问 SQLite、Topic 或执行设备命令；
 * 新人应从start()和dispatch()阅读请求边界。
 */
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>

namespace core::app {
class ApplicationController;
class StorageRuntime;
}

namespace core::web {

/** @brief 在Qt主事件循环处理有界配置与任务HTTP请求的具体服务。 */
class DeviceRegistryHttpService final : public QObject
{
    Q_OBJECT
public:
    /** @brief 创建未监听且未绑定StorageRuntime的服务。 */
    explicit DeviceRegistryHttpService(QObject *parent = nullptr);

    /**
     * @brief 绑定本机HTTP端口和唯一存储组合根。
     * @param port 监听端口；0用于测试自动分配。
     * @param storageRuntime 已移动到存储线程且完成initialize的唯一Owner。
     * @param applicationController 与本服务同属主线程且覆盖所有在途请求的唯一任务编排者。
     * @return 监听成功返回true。
     */
    bool start(quint16 port,
               core::app::StorageRuntime &storageRuntime,
               core::app::ApplicationController &applicationController);

    /**
     * @brief 停止受理并收口已接受请求，再撤销借用关系；断开的客户端不取消已发生的提交或回滚。
     * @note 仅关停路径允许同步存储屏障并定向派发完成回调，不运行嵌套事件循环；必须先于存储销毁调用。
     */
    void stop();

    /** @brief 返回实际监听端口，未启动时为0。 */
    quint16 port() const noexcept;

    /** @brief 返回最近监听或跨线程调用失败原因。 */
    QString lastError() const;

private Q_SLOTS:
    /** @brief 接受所有待处理连接，并为每个连接安装有界单请求读取器。 */
    void acceptConnections();

private:
    /** @brief 在收到完整请求头和Content-Length正文后执行一次路由并关闭连接。 */
    void consumeRequest(QTcpSocket &socket);

    /**
     * @brief 将路由分为存储 CRUD 与主线程任务编排；完成回调始终在 HTTP 主线程执行一次。
     * @param method 原请求方法。
     * @param path 未解码的 V1 路径。
     * @param body 不超过 64 KiB 的请求正文值副本。
     * @param completed V1 JSON 完成回调；服务在其完成前保持所有借用对象存活。
     */
    void dispatch(const QByteArray &method, const QString &path,
                  const QByteArray &body, std::function<void(QByteArray)> completed);

    /** @brief 写出固定JSON响应头和正文，不启用keep-alive或分块传输。 */
    static void writeResponse(QTcpSocket &socket, int statusCode, const QByteArray &body);

    QTcpServer m_server; ///< 主线程唯一HTTP监听Socket。
    core::app::StorageRuntime *m_storageRuntime = nullptr; ///< 仅在服务启动期借用的存储线程组合根。
    core::app::ApplicationController *m_applicationController = nullptr; ///< 主线程唯一采集编排入口。
    QString m_lastError; ///< 最近边界失败原因。
    int m_pendingRequests = 0; ///< 已受理未收口请求数，上限 16；客户端断开不提前释放名额。
    bool m_mutationPending = false; ///< HTTP 写请求互斥位，覆盖存储提交、主线程应用及失败回滚，不保存命令队列。
    bool m_stopping = false; ///< 关停期间拒绝新请求，完成回调收口前不撤销存储借用。
};

} // namespace core::web
