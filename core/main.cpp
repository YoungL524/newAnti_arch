#include "core/app/ApplicationRuntime.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
/**
 * @brief 装配正式内部UDP、共享数据域及V1 WebReader；同一主事件循环负责轻量I/O和调度。
 * @details 栈对象按依赖逆序释放；绑定失败返回非零。部署或隔离联试可用NEWANTI_DATABASE_PATH
 * 指定SQLite文件，未设置时仍使用系统应用数据目录。航迹入口未绑定真实采集会话时拒绝发布，不伪造运行时ID。
 * 一键部署时只通过当前用户本地 IPC 报告就绪及接收 STOP；停机仍复用运行时的归档排空，不增加设备控制路径。
 */
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NewAntiDrone"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0.0"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("launcher-channel"), QStringLiteral("部署生命周期 IPC 名称"), QStringLiteral("name")});
    parser.addOption({QStringLiteral("launcher-token"), QStringLiteral("当前部署实例身份"), QStringLiteral("token")});
    parser.process(app);
    QLocalSocket lifecycle;
    bool discardLifecycleLine = false; ///< 超长未完成行一直丢弃至换行，不能把其后缀误识别为 STOP。
    const bool managed = parser.isSet(QStringLiteral("launcher-channel"));
    if (managed != parser.isSet(QStringLiteral("launcher-token"))) return 1;
    if (managed) {
        lifecycle.connectToServer(parser.value(QStringLiteral("launcher-channel")));
        if (!lifecycle.waitForConnected(3000)) return 1;
        lifecycle.setReadBufferSize(129);
        /** @brief 管理连接只接受完整 STOP 行；连接失效时退出，避免留下脱离启动器的 Core。 */
        QObject::connect(&lifecycle, &QLocalSocket::readyRead, &app, [&] {
            while (lifecycle.canReadLine()) {
                const QByteArray line = lifecycle.readLine(129);
                if (discardLifecycleLine) {
                    if (line.endsWith('\n')) discardLifecycleLine = false;
                    continue;
                }
                if (line == QByteArrayLiteral("STOP\n")) app.quit();
                else qWarning() << "忽略无效生命周期指令";
            }
            if (lifecycle.bytesAvailable() > 128) {
                lifecycle.readAll();
                discardLifecycleLine = true;
                qWarning() << "忽略超长生命周期指令";
            }
        });
        QObject::connect(&lifecycle, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    }
    /** @brief 只报告生命周期事实，令牌校验在启动器；启动错误正文有界，不通过普通 Topic 发送请求。 */
    const auto report = [&](const QString &type, const QString &message = QString()) {
        if (!managed) return;
        lifecycle.write(QJsonDocument(QJsonObject {
            {QStringLiteral("type"), type},
            {QStringLiteral("token"), parser.value(QStringLiteral("launcher-token"))},
            {QStringLiteral("message"), message.left(512)}
        }).toJson(QJsonDocument::Compact) + '\n');
        lifecycle.flush();
        if (lifecycle.bytesToWrite() > 0) lifecycle.waitForBytesWritten(1000);
    };
    report(QStringLiteral("core-connected"));
    const QString databaseOverride = qEnvironmentVariable("NEWANTI_DATABASE_PATH");
    const QString dataDirectory = databaseOverride.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        : QFileInfo(databaseOverride).absolutePath();
    if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
        qCritical() << QStringLiteral("无法创建应用数据目录");
        report(QStringLiteral("core-error"), QStringLiteral("无法创建应用数据目录"));
        return 1;
    }
    core::app::ApplicationRuntime runtime(
        databaseOverride.isEmpty()
            ? QDir(dataDirectory).filePath(QStringLiteral("antidrone.db"))
            : databaseOverride,
        QCoreApplication::applicationVersion());
    if (!runtime.start()) {
        qCritical().noquote() << runtime.lastError();
        report(QStringLiteral("core-error"), runtime.lastError());
        return 1;
    }
    if (managed && lifecycle.state() != QLocalSocket::ConnectedState) return 1;
    bool shutdownFailed = false;
    /** @brief 正常退出复用原有归档排空；失败同时报告管理端并保留非零进程退出码。 */
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        runtime.stop();
        shutdownFailed = !runtime.lastError().isEmpty();
        if (shutdownFailed) report(QStringLiteral("core-stop-error"), runtime.lastError());
    });
    report(QStringLiteral("core-ready"));
    if (managed && lifecycle.state() != QLocalSocket::ConnectedState) return 1;
    qInfo() << QStringLiteral(
        "内部UDP 127.0.0.1:7788；HTTP http://127.0.0.1:8088/api/v1/devices 与 /api/v1/tasks；"
        "V1 WebSocket ws://127.0.0.1:8089/ws/v1");
    const int result = app.exec();
    return shutdownFailed ? 1 : result;
}
