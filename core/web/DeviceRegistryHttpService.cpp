#include "core/web/DeviceRegistryHttpService.h"
#include "core/web/DeviceRegistryHttpService.h"
#include "core/app/ApplicationController.h"
#include "core/app/StorageRuntime.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QMetaObject>
#include <QTcpSocket>

namespace core::web {

namespace {
constexpr qint64 kMaximumRequestBytes = 64 * 1024;

/**
 * @brief 生成不经过 StorageRuntime 的 HTTP 边界错误，保持 V1 结构化合同。
 */
QByteArray errorBody(const char *code, const QString &message)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject {{QStringLiteral("code"), QString::fromLatin1(code)},
                                                {QStringLiteral("message"), message}}}})
        .toJson(QJsonDocument::Compact);
}

/**
 * @brief 生成采集命令成功响应，不复制任务或设备运行状态。
 */
QByteArray acquisitionBody(bool acquisition)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("ok"), true},
        {QStringLiteral("data"), QJsonObject {
            {QStringLiteral("acquisition"), acquisition}}}})
        .toJson(QJsonDocument::Compact);
}
}

/**
 * @brief 连接QTcpServer的新连接通知，构造阶段不监听端口。
 */
DeviceRegistryHttpService::DeviceRegistryHttpService(QObject *parent)
    : QObject(parent)
{
    QObject::connect(&m_server, &QTcpServer::newConnection,
                     this, &DeviceRegistryHttpService::acceptConnections);
}

/**
 * @brief 保存已初始化StorageRuntime借用关系后仅监听Loopback，避免配置接口暴露到现场网络。
 */
bool DeviceRegistryHttpService::start(
    quint16 port,
    core::app::StorageRuntime &storageRuntime,
    core::app::ApplicationController &applicationController)
{
    if (m_server.isListening() || m_storageRuntime || m_applicationController) {
        m_lastError = QStringLiteral("设备台账HTTP服务已经启动");
        return false;
    }
    m_storageRuntime = &storageRuntime;
    m_applicationController = &applicationController;
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        m_lastError = m_server.errorString();
        m_storageRuntime = nullptr;
        m_applicationController = nullptr;
        return false;
    }
    m_lastError.clear();
    return true;
}

/**
 * @brief 先关闭监听与现有连接，再撤销跨线程Owner借用关系。
 */
void DeviceRegistryHttpService::stop()
{
    m_server.close();
    const auto sockets = m_server.findChildren<QTcpSocket*>();
    for (QTcpSocket *socket : sockets) socket->disconnectFromHost();
    m_storageRuntime = nullptr;
    m_applicationController = nullptr;
}

/**
 * @brief 将QTcpServer分配的端口作为只读诊断值返回。
 */
quint16 DeviceRegistryHttpService::port() const noexcept
{
    return m_server.isListening() ? m_server.serverPort() : 0;
}

/**
 * @brief 返回最近错误的值副本，避免调用方保存内部QString引用。
 */
QString DeviceRegistryHttpService::lastError() const
{
    return m_lastError;
}

/**
 * @brief 为每个连接设置父对象和读取回调；超过上限立即返回413并关闭。
 */
void DeviceRegistryHttpService::acceptConnections()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        socket->setParent(&m_server);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            if (socket->bytesAvailable() + socket->property("requestSize").toLongLong()
                > kMaximumRequestBytes) {
                writeResponse(*socket, 413, errorBody("PAYLOAD_TOO_LARGE", QStringLiteral("请求过大")));
                return;
            }
            consumeRequest(*socket);
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

/**
 * @brief 累积单个有Content-Length的HTTP/1请求；不支持流水线、chunked或升级协议。
 * @details 设备配置请求低频且小于64KiB，完整后立即路由并关闭，避免引入通用Web框架和连接状态机。
 */
void DeviceRegistryHttpService::consumeRequest(QTcpSocket &socket)
{
    QByteArray request = socket.property("requestBuffer").toByteArray();
    request.append(socket.readAll());
    socket.setProperty("requestBuffer", request);
    socket.setProperty("requestSize", request.size());
    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const QList<QByteArray> lines = request.left(headerEnd).split('\n');
    if (lines.isEmpty()) {
        writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("请求行缺失")));
        return;
    }
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("请求行无效")));
        return;
    }
    qint64 contentLength = 0;
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            bool ok = false;
            contentLength = trimmed.mid(sizeof("content-length:") - 1).trimmed().toLongLong(&ok);
            if (!ok || contentLength < 0 || contentLength > kMaximumRequestBytes) {
                writeResponse(socket, 400, errorBody("INVALID_REQUEST", QStringLiteral("Content-Length无效")));
                return;
            }
        }
    }
    const qsizetype bodyOffset = headerEnd + 4;
    if (request.size() - bodyOffset < contentLength) return;
    int statusCode = 200;
    const QByteArray body = request.mid(bodyOffset, contentLength);
    const QByteArray response = dispatch(requestLine[0], QString::fromUtf8(requestLine[1]), body, statusCode);
    writeResponse(socket, statusCode, response);
}

/**
 * @brief 只路由设备、任务与只读任务模板的冻结V1子集，其他路径和方法明确返回404或405。
 */
QByteArray DeviceRegistryHttpService::dispatch(
    const QByteArray &method, const QString &path,
    const QByteArray &body, int &statusCode)
{
    if (!m_storageRuntime || !m_applicationController) {
        statusCode = 503;
        return errorBody("SERVICE_UNAVAILABLE", QStringLiteral("设备服务不可用"));
    }
    QByteArray response;
    bool invoked = false;
    if (path == QStringLiteral("/api/v1/devices") && method == QByteArrayLiteral("GET")) {
        invoked = QMetaObject::invokeMethod(m_storageRuntime, "listDevices",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response));
    } else if (path == QStringLiteral("/api/v1/devices") && method == QByteArrayLiteral("POST")) {
        invoked = QMetaObject::invokeMethod(m_storageRuntime, "createDevice",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response), Q_ARG(QByteArray, body));
    } else if (path.startsWith(QStringLiteral("/api/v1/devices/"))) {
        const QString uuid = path.mid(QStringLiteral("/api/v1/devices/").size());
        if (method == QByteArrayLiteral("GET")) {
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "getDevice",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response),
                Q_ARG(QString, uuid));
        } else if (method == QByteArrayLiteral("PUT")) {
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "updateDevice",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response),
                Q_ARG(QString, uuid), Q_ARG(QByteArray, body));
        } else if (method == QByteArrayLiteral("DELETE")) {
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "deleteDevice",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response),
                Q_ARG(QString, uuid), Q_ARG(QByteArray, body));
        } else {
            statusCode = 405;
            return errorBody("METHOD_NOT_ALLOWED", QStringLiteral("方法不允许"));
        }
    } else if (path == QStringLiteral("/api/v1/task-templates")
               && method == QByteArrayLiteral("GET")) {
        invoked = QMetaObject::invokeMethod(m_storageRuntime, "listTaskTemplates",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response));
    } else if (path == QStringLiteral("/api/v1/task-templates")) {
        statusCode = 405;
        return errorBody("METHOD_NOT_ALLOWED", QStringLiteral("V1模板只允许读取"));
    } else if (path == QStringLiteral("/api/v1/tasks") && method == QByteArrayLiteral("GET")) {
        invoked = QMetaObject::invokeMethod(m_storageRuntime, "listTasks",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response));
    } else if (path == QStringLiteral("/api/v1/tasks") && method == QByteArrayLiteral("POST")) {
        invoked = QMetaObject::invokeMethod(m_storageRuntime, "createTask",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response), Q_ARG(QByteArray, body));
    } else if (path == QStringLiteral("/api/v1/tasks/active") && method == QByteArrayLiteral("GET")) {
        invoked = QMetaObject::invokeMethod(m_storageRuntime, "activeTask",
            Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response));
    } else if (path.startsWith(QStringLiteral("/api/v1/tasks/"))) {
        const QString taskPath = path.mid(QStringLiteral("/api/v1/tasks/").size());
        const QString activateSuffix = QStringLiteral("/activate");
        if (taskPath.endsWith(activateSuffix) && method == QByteArrayLiteral("POST")) {
            const QString uuid = taskPath.left(taskPath.size() - activateSuffix.size());
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "activateTask",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response), Q_ARG(QString, uuid));
        } else if (taskPath.endsWith(QStringLiteral("/acquisition/start"))
                   && method == QByteArrayLiteral("POST")) {
            const QString suffix = QStringLiteral("/acquisition/start");
            const QUuid uuid(taskPath.left(taskPath.size() - suffix.size()));
            invoked = true;
            if (!uuid.isNull() && m_applicationController->startAcquisition(*m_storageRuntime, uuid)) {
                response = acquisitionBody(true);
            } else {
                response = errorBody(uuid.isNull() ? "INVALID_REQUEST" : "COMMAND_REJECTED", uuid.isNull()
                    ? QStringLiteral("任务UUID无效")
                    : m_applicationController->lastError());
            }
        } else if (taskPath.endsWith(QStringLiteral("/acquisition/stop"))
                   && method == QByteArrayLiteral("POST")) {
            const QString suffix = QStringLiteral("/acquisition/stop");
            const QUuid uuid(taskPath.left(taskPath.size() - suffix.size()));
            invoked = true;
            if (!uuid.isNull() && m_applicationController->stopAcquisition(*m_storageRuntime, uuid)) {
                response = acquisitionBody(false);
            } else {
                response = errorBody(uuid.isNull() ? "INVALID_REQUEST" : "COMMAND_REJECTED", uuid.isNull()
                    ? QStringLiteral("任务UUID无效")
                    : m_applicationController->lastError());
            }
        } else if (taskPath.endsWith(QStringLiteral("/complete"))
                   && method == QByteArrayLiteral("POST")) {
            const QString uuid = taskPath.left(taskPath.size() - QStringLiteral("/complete").size());
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "completeTask",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response), Q_ARG(QString, uuid));
        } else if (taskPath.endsWith(QStringLiteral("/archive"))
                   && method == QByteArrayLiteral("POST")) {
            const QString uuid = taskPath.left(taskPath.size() - QStringLiteral("/archive").size());
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "archiveTask",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response), Q_ARG(QString, uuid));
        } else if (method == QByteArrayLiteral("DELETE") && !taskPath.contains('/')) {
            invoked = QMetaObject::invokeMethod(m_storageRuntime, "deleteTask",
                Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, response), Q_ARG(QString, taskPath));
        } else {
            statusCode = 405;
            return errorBody("METHOD_NOT_ALLOWED", QStringLiteral("方法不允许"));
        }
    } else {
        statusCode = 404;
        return errorBody("RESOURCE_NOT_FOUND", QStringLiteral("接口不存在"));
    }
    if (!invoked) {
        statusCode = 503;
        return errorBody("SERVICE_UNAVAILABLE", QStringLiteral("存储线程调用失败"));
    }
    const QJsonObject object = QJsonDocument::fromJson(response).object();
    if (!object.value(QStringLiteral("ok")).toBool()) {
        const QString code = object.value(QStringLiteral("error")).toObject()
                                 .value(QStringLiteral("code")).toString();
        statusCode = code == QStringLiteral("INVALID_REQUEST") ? 400
            : code == QStringLiteral("RESOURCE_NOT_FOUND") ? 404
            : code == QStringLiteral("REVISION_CONFLICT") ? 409
            : code == QStringLiteral("SERVICE_UNAVAILABLE") ? 503 : 422;
    } else if (method == QByteArrayLiteral("POST")
               && (path == QStringLiteral("/api/v1/devices")
                   || path == QStringLiteral("/api/v1/tasks"))) {
        statusCode = 201;
    }
    return response;
}

/**
 * @brief 输出最小HTTP/1.1 JSON响应并主动关闭，避免未实现keep-alive产生悬挂连接。
 */
void DeviceRegistryHttpService::writeResponse(
    QTcpSocket &socket, int statusCode, const QByteArray &body)
{
    const QByteArray reason = statusCode == 200 ? QByteArrayLiteral("OK")
        : statusCode == 400 ? QByteArrayLiteral("Bad Request")
        : statusCode == 404 ? QByteArrayLiteral("Not Found")
        : statusCode == 405 ? QByteArrayLiteral("Method Not Allowed")
        : statusCode == 409 ? QByteArrayLiteral("Conflict")
        : statusCode == 413 ? QByteArrayLiteral("Payload Too Large")
        : statusCode == 422 ? QByteArrayLiteral("Unprocessable Entity")
        : QByteArrayLiteral("Service Unavailable");
    QByteArray header = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode) + ' ' + reason
        + QByteArrayLiteral("\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n");
    socket.write(header);
    socket.write(body);
    socket.disconnectFromHost();
}

} // namespace core::web
