/**
 * @file DeviceRegistryHttpService.h
 * @brief 定义设备台账与任务登记使用的最小HTTP入口。
 * @details 上游React调用/api/v1/devices和/api/v1/tasks，下游通过阻塞跨线程调用把低频请求交给StorageRuntime中的
 * DeviceConfigService或TaskService唯一Owner。本类只拥有监听Socket和HTTP边界，不访问SQLite、Topic或Command控制平面；
 * 新人应从start()和dispatch()阅读请求边界。
 */
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace core::app { class StorageRuntime; }

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
     * @return 监听成功返回true。
     */
    bool start(quint16 port, core::app::StorageRuntime &storageRuntime);

    /** @brief 停止接受新请求并清除StorageRuntime借用指针。 */
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

    /** @brief 把允许的设备及任务登记请求同步交给存储线程唯一Owner。 */
    QByteArray dispatch(const QByteArray &method, const QString &path,
                        const QByteArray &body, int &statusCode);

    /** @brief 写出固定JSON响应头和正文，不启用keep-alive或分块传输。 */
    static void writeResponse(QTcpSocket &socket, int statusCode, const QByteArray &body);

    QTcpServer m_server; ///< 主线程唯一HTTP监听Socket。
    core::app::StorageRuntime *m_storageRuntime = nullptr; ///< 仅在服务启动期借用的存储线程组合根。
    QString m_lastError; ///< 最近边界失败原因。
};

} // namespace core::web
