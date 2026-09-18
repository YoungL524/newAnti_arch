#include "core/app/ApplicationRuntime.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
/**
 * @brief 装配正式内部UDP、共享数据域及V1 WebReader；同一主事件循环负责轻量I/O和调度。
 * @details 栈对象按依赖逆序释放；绑定失败返回非零。航迹入口未绑定真实采集会话时拒绝发布，不伪造运行时ID。
 */
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NewAntiDrone"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0.0"));
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
        qCritical() << QStringLiteral("无法创建应用数据目录");
        return 1;
    }
    core::app::ApplicationRuntime runtime(
        QDir(dataDirectory).filePath(QStringLiteral("antidrone.db")),
        QCoreApplication::applicationVersion());
    if (!runtime.start()) {
        qCritical().noquote() << runtime.lastError();
        return 1;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&runtime] { runtime.stop(); });
    qInfo() << QStringLiteral(
        "内部UDP 127.0.0.1:7788；HTTP http://127.0.0.1:8088/api/v1/devices 与 /api/v1/tasks；"
        "V1 WebSocket ws://127.0.0.1:8089/ws/v1");
    return app.exec();
}
