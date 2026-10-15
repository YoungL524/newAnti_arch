#include "core/web/DeviceRegistryHttpService.h"
#include "core/app/ApplicationController.h"
#include "core/app/StorageRuntime.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <QUuid>

namespace core::web {
namespace {
constexpr qint64 kMaximumRequestBytes = 64 * 1024;
constexpr int kMaximumPendingRequests = 16;
constexpr int kMaximumConnections = 32;

/**
 * @brief 在 HTTP 边界生成稳定错误结果，不向 Topic 投递请求或伪造设备结果。
 */
QByteArray errorBody(const char *code, const QString &message)
{
    return QJsonDocument(QJsonObject{{"ok", false}, {"error", QJsonObject{
        {"code", QString::fromLatin1(code)}, {"message", message}}}}).toJson(QJsonDocument::Compact);
}

/**
 * @brief 保留 V1 状态码映射，异步等待不提前返回成功；只有设备或任务创建使用 201。
 */
int responseStatus(const QByteArray &method, const QString &path, const QByteArray &response)
{
    const auto object = QJsonDocument::fromJson(response).object();
    if (object.value(QStringLiteral("ok")).toBool()) {
        return method == QByteArrayLiteral("POST")
            && (path == QStringLiteral("/api/v1/devices") || path == QStringLiteral("/api/v1/tasks")) ? 201 : 200;
    }
    const QString code = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString();
    if (code == QStringLiteral("INVALID_REQUEST")) return 400;
    if (code == QStringLiteral("RESOURCE_NOT_FOUND")) return 404;
    if (code == QStringLiteral("METHOD_NOT_ALLOWED")) return 405;
    if (code == QStringLiteral("REVISION_CONFLICT") || code == QStringLiteral("TASK_STATE_CONFLICT")
        || code == QStringLiteral("REQUEST_BUSY")) return 409;
    if (code == QStringLiteral("PERSISTENCE_FAILED") || code == QStringLiteral("INTERNAL_ERROR")) return 500;
    if (code.isEmpty() || code == QStringLiteral("SERVICE_UNAVAILABLE")) return 503;
    return 422;
}
}

/**
 * @brief HTTP 监听和完成回调均留在主线程，不创建专用 HTTP 执行线程。
 */
DeviceRegistryHttpService::DeviceRegistryHttpService(QObject *parent) : QObject(parent)
{
    QObject::connect(&m_server, &QTcpServer::newConnection,
                     this, &DeviceRegistryHttpService::acceptConnections);
}

/**
 * @brief 只有存储完成启动后才借用两个 Owner，运行期通过异步阶段访问而不阻塞主循环。
 */
bool DeviceRegistryHttpService::start(quint16 port, core::app::StorageRuntime &storageRuntime,
                                       core::app::ApplicationController &applicationController)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_server.isListening() || m_storageRuntime || m_applicationController) {
        m_lastError = QStringLiteral("设备台账HTTP服务已经启动");
        return false;
    }
    if (!storageRuntime.thread() || !storageRuntime.thread()->isRunning()
        || storageRuntime.thread() == thread() || applicationController.thread() != thread()) {
        m_lastError = QStringLiteral("HTTP和控制编排必须同属主线程，存储必须使用已运行的独立线程");
        return false;
    }
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        m_lastError = m_server.errorString();
        return false;
    }
    m_storageRuntime = &storageRuntime;
    m_applicationController = &applicationController;
    m_stopping = false;
    m_lastError.clear();
    return true;
}

/**
 * @brief 先停止受理并断开客户端，再只收口本服务已接受的异步操作，最后撤销 Owner 借用。
 * @details 关停屏障等存储已有阶段结束，再定向派发编排者及 HTTP 的 MetaCall；回滚若再次入队便进行下一轮。
 * 不派发普通网络、计时器或嵌套事件循环。运行期 dispatch 绝不使用此屏障，最多保留 16 项低频请求。
 */
void DeviceRegistryHttpService::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_stopping) return;
    m_stopping = true;
    m_server.close();
    for (QTcpSocket *socket : m_server.findChildren<QTcpSocket*>()) socket->abort();
    while (m_pendingRequests > 0) {
        QMetaObject::invokeMethod(m_storageRuntime, [] {}, Qt::BlockingQueuedConnection);
        QCoreApplication::sendPostedEvents(m_applicationController, QEvent::MetaCall);
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
    m_storageRuntime = nullptr;
    m_applicationController = nullptr;
}

/**
 * @brief 返回实际监听结果，关停后不继续暴露配置端口。
 */
quint16 DeviceRegistryHttpService::port() const noexcept
{
    return m_server.isListening() ? m_server.serverPort() : 0;
}

/**
 * @brief 读取主线程维护的监听失败原因，不跨线程读取存储可变错误文本。
 */
QString DeviceRegistryHttpService::lastError() const
{
    return m_lastError;
}

/**
 * @brief 连接数限制为 32，未提交完整请求的连接 10 秒后关闭；每连接只允许受理一个请求。
 * @details 已受理请求的寿命与 Socket 分离，断开不会让另一个写请求越过尚未结束的回滚。
 */
void DeviceRegistryHttpService::acceptConnections()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        socket->setParent(&m_server);
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        if (m_stopping || m_server.findChildren<QTcpSocket*>().size() > kMaximumConnections) {
            writeResponse(*socket, 503, errorBody("SERVICE_UNAVAILABLE", QStringLiteral("HTTP连接已达上限")));
            continue;
        }
        socket->setReadBufferSize(kMaximumRequestBytes + 1);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->property("requestDispatched").toBool()) {
                socket->abort();
                return;
            }
            if (socket->bytesAvailable() + socket->property("requestSize").toLongLong() > kMaximumRequestBytes) {
                socket->setProperty("requestDispatched", true);
                writeResponse(*socket, 413, errorBody("PAYLOAD_TOO_LARGE", QStringLiteral("请求过大")));
                return;
            }
            consumeRequest(*socket);
        });
        const QPointer<QTcpSocket> guardedSocket(socket);
        QTimer::singleShot(10000, socket, [guardedSocket] {
            if (guardedSocket && !guardedSocket->property("requestDispatched").toBool()) guardedSocket->abort();
        });
    }
}

/**
 * @brief 完整解析一次有界 HTTP 请求后异步处理；最多 16 项在途，所有写操作互斥且不排队。
 * @details Socket 仅由主线程 QPointer 检查；完成时即使客户端已断开仍释放请求名额和写互斥位。
 */
void DeviceRegistryHttpService::consumeRequest(QTcpSocket &socket)
{
    QByteArray request = socket.property("requestBuffer").toByteArray();
    request.append(socket.readAll());
    socket.setProperty("requestBuffer", request);
    socket.setProperty("requestSize", request.size());
    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const auto lines = request.left(headerEnd).split('\n');
    const auto requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        socket.setProperty("requestDispatched", true);
        writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("请求行无效")));
        return;
    }
    qint64 contentLength = 0;
    bool lengthSeen = false;
    for (const auto &line : lines) {
        const auto trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("transfer-encoding:")) {
            socket.setProperty("requestDispatched", true);
            writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("不支持Transfer-Encoding")));
            return;
        }
        if (trimmed.toLower().startsWith("content-length:")) {
            if (lengthSeen) {
                socket.setProperty("requestDispatched", true);
                writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("Content-Length重复")));
                return;
            }
            lengthSeen = true;
            bool ok = false;
            contentLength = trimmed.mid(sizeof("content-length:") - 1).trimmed().toLongLong(&ok);
            if (!ok || contentLength < 0 || contentLength > kMaximumRequestBytes) {
                socket.setProperty("requestDispatched", true);
                writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("Content-Length无效")));
                return;
            }
        }
    }
    const qsizetype bodyOffset = headerEnd + 4;
    if (request.size() - bodyOffset < contentLength) return;
    socket.setProperty("requestDispatched", true);
    socket.setProperty("requestBuffer", QByteArray());
    const QByteArray method = requestLine[0];
    const QString path = QString::fromUtf8(requestLine[1]);
    const bool mutation = method != QByteArrayLiteral("GET");
    if (m_stopping || m_pendingRequests >= kMaximumPendingRequests) {
        writeResponse(socket, 503, errorBody("SERVICE_UNAVAILABLE", QStringLiteral("HTTP请求已达上限或服务正在停止")));
        return;
    }
    if (mutation && m_mutationPending) {
        writeResponse(socket, 409, errorBody("REQUEST_BUSY", QStringLiteral("已有写请求正在处理，请等待完成后重试")));
        return;
    }
    ++m_pendingRequests;
    if (mutation) m_mutationPending = true;
    const QPointer<QTcpSocket> guardedSocket(&socket);
    dispatch(method, path, request.mid(bodyOffset, contentLength),
        [this, guardedSocket, method, path, mutation](QByteArray response) {
            const auto object = QJsonDocument::fromJson(response).object();
            if (!object.value(QStringLiteral("ok")).toBool()
                && object.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString().isEmpty()) {
                response = errorBody("SERVICE_UNAVAILABLE", QStringLiteral("内部调用未返回有效结果"));
            }
            if (!m_stopping && guardedSocket && guardedSocket->state() == QAbstractSocket::ConnectedState) {
                writeResponse(*guardedSocket, responseStatus(method, path, response), response);
            }
            if (mutation) m_mutationPending = false;
            --m_pendingRequests;
        });
}

/**
 * @brief CRUD 与有界归档查询异步交给存储 Owner，控制路由只调用主线程 ApplicationController。
 * @details 请求正文和路径按值进入 Qt 低频调用；没有共享 CommandQueue、额外线程或对 Topic 的轮询。
 */
void DeviceRegistryHttpService::dispatch(const QByteArray &method, const QString &path,
    const QByteArray &body, std::function<void(QByteArray)> completed)
{
    if (!m_storageRuntime || !m_applicationController) {
        completed(errorBody("SERVICE_UNAVAILABLE", QStringLiteral("设备服务不可用")));
        return;
    }
    auto *storage = m_storageRuntime;
    std::function<QByteArray()> work;
    if (path.section('?', 0, 0) == QStringLiteral("/api/v1/archive/tracks")) {
        if (method == "GET") work = [storage, path] { return storage->listArchivedTracks(path); };
    } else if (path.section('?', 0, 0) == QStringLiteral("/api/v1/archive/events")) {
        if (method == "GET") work = [storage, path] { return storage->listArchivedEvents(path); };
    } else if (path.section('?', 0, 0) == QStringLiteral("/api/v1/archive/runs")) {
        if (method == "GET") work = [storage, path] { return storage->listArchivedRuns(path); };
    } else if (path == QStringLiteral("/api/v1/devices")) {
        if (method == "GET") work = [storage] { return storage->listDevices(); };
        else if (method == "POST") work = [storage, body] { return storage->createDevice(body); };
    } else if (path.startsWith(QStringLiteral("/api/v1/devices/"))) {
        const QString uuid = path.mid(QStringLiteral("/api/v1/devices/").size());
        if (method == "GET") work = [storage, uuid] { return storage->getDevice(uuid); };
        else if (method == "PUT") work = [storage, uuid, body] { return storage->updateDevice(uuid, body); };
        else if (method == "DELETE") work = [storage, uuid, body] { return storage->deleteDevice(uuid, body); };
    } else if (path == QStringLiteral("/api/v1/task-templates")) {
        if (method == "GET") work = [storage] { return storage->listTaskTemplates(); };
    } else if (path == QStringLiteral("/api/v1/tasks")) {
        if (method == "GET") work = [storage] { return storage->listTasks(); };
        else if (method == "POST") work = [storage, body] { return storage->createTask(body); };
    } else if (path == QStringLiteral("/api/v1/tasks/active")) {
        if (method == "GET") work = [storage] { return storage->activeTask(); };
    } else if (path.startsWith(QStringLiteral("/api/v1/tasks/"))) {
        const QString taskPath = path.mid(QStringLiteral("/api/v1/tasks/").size());
        const QString action = taskPath.contains('/') ? taskPath.mid(taskPath.indexOf('/') + 1) : QString();
        const QString id = taskPath.section('/', 0, 0);
        const QUuid uuid(id);
        if (method == "POST" && action == QStringLiteral("activate")) {
            m_applicationController->activateTask(*storage, uuid, completed);
            return;
        }
        if (method == "POST" && action == QStringLiteral("complete")) {
            m_applicationController->completeTask(*storage, uuid, completed);
            return;
        }
        if (method == "POST" && action == QStringLiteral("acquisition/start")) {
            m_applicationController->startAcquisition(*storage, uuid, completed);
            return;
        }
        if (method == "POST" && action == QStringLiteral("acquisition/stop")) {
            m_applicationController->stopAcquisition(*storage, uuid, completed);
            return;
        }
        if (method == "PUT" && action == QStringLiteral("control-settings")) {
            if (uuid.isNull()) completed(errorBody("INVALID_REQUEST", QStringLiteral("任务UUID无效")));
            else m_applicationController->updateTaskControlSettings(*storage, uuid, body, completed);
            return;
        }
        if (method == "POST" && action == QStringLiteral("archive")) {
            work = [storage, id] { return storage->archiveTask(id); };
        } else if (method == "DELETE" && !taskPath.contains('/')) {
            work = [storage, id] { return storage->deleteTask(id); };
        }
    } else {
        completed(errorBody("RESOURCE_NOT_FOUND", QStringLiteral("接口不存在")));
        return;
    }
    if (!work) {
        completed(errorBody("METHOD_NOT_ALLOWED", QStringLiteral("方法不允许")));
        return;
    }
    const bool queued = QMetaObject::invokeMethod(storage, [this, work, completed] {
        const QByteArray response = work();
        QMetaObject::invokeMethod(this, [completed, response] { completed(response); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) completed(errorBody("SERVICE_UNAVAILABLE", QStringLiteral("存储线程调用失败")));
}

/**
 * @brief 在主线程输出一次 JSON 响应并关闭连接，客户端断开不触发业务重试。
 */
void DeviceRegistryHttpService::writeResponse(QTcpSocket &socket, int statusCode, const QByteArray &body)
{
    const QByteArray reason = statusCode == 200 ? QByteArrayLiteral("OK")
        : statusCode == 201 ? QByteArrayLiteral("Created")
        : statusCode == 400 ? QByteArrayLiteral("Bad Request")
        : statusCode == 404 ? QByteArrayLiteral("Not Found")
        : statusCode == 405 ? QByteArrayLiteral("Method Not Allowed")
        : statusCode == 409 ? QByteArrayLiteral("Conflict")
        : statusCode == 413 ? QByteArrayLiteral("Payload Too Large")
        : statusCode == 422 ? QByteArrayLiteral("Unprocessable Entity")
        : statusCode == 500 ? QByteArrayLiteral("Internal Server Error")
        : QByteArrayLiteral("Service Unavailable");
    socket.write(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode) + ' ' + reason
        + QByteArrayLiteral("\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n"));
    socket.write(body);
    socket.disconnectFromHost();
}

} // namespace core::web
