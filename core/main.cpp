#include "core/app/ApplicationRuntime.h"
#include <QCoreApplication>
#include <QDebug>
/**
 * @brief 装配正式心跳链；同一主事件循环运行通信、状态owner及WebReader。
 * @details 栈对象按依赖逆序释放；绑定失败返回非零。当前仅接收心跳，Task/System尚无owner不写假状态。
 */
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    core::app::ApplicationRuntime runtime;
    if (!runtime.start()) {
        qCritical().noquote() << runtime.lastError();
        return 1;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&runtime] { runtime.stop(); });
    qInfo() << QStringLiteral("心跳UDP 127.0.0.1:7788；状态WebSocket ws://127.0.0.1:8090");
    return app.exec();
}
