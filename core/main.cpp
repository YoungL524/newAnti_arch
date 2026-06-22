#include <QCoreApplication>
#include <QDebug>
#include <csignal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "AppController.h"
#include "StdinReader.h"
#include "RadarWorkModeController.h"
#include "log/Logger.h"

#ifdef Q_OS_WIN
static void configureWindowsConsoleEncoding()
{
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}
#endif

static void printBanner()
{
    qDebug() << QStringLiteral("========================================");
    qDebug() << QStringLiteral("  NewAntiDrone-core v1.0.0");
    qDebug() << QStringLiteral("========================================");
}

static void printUsage()
{
    qDebug() << QStringLiteral("\n========================================");
    qDebug() << QStringLiteral("  NewAntiDrone-core v1.0.0 — 命令行控制");
    qDebug() << QStringLiteral("========================================");
    qDebug() << QStringLiteral("业务说明:");
    qDebug() << QStringLiteral("  - 本端每 50ms 向雷达端发送 DPH");
    qDebug() << QStringLiteral("  - 发送 work 后雷达端开始上报航迹；standby 后停止");
    qDebug() << QStringLiteral("----------------------------------------");
    qDebug() << QStringLiteral("命令列表:");
    qDebug() << QStringLiteral("  1 / work       - 发送工作模式 (workStandby_Mode=1)");
    qDebug() << QStringLiteral("  2 / standby    - 发送待机模式 (workStandby_Mode=0)");
    qDebug() << QStringLiteral("  h / help       - 显示帮助");
    qDebug() << QStringLiteral("  q / quit       - 退出程序");
    qDebug() << QStringLiteral("========================================\n");
}

static void handleConsoleCommand(AppController& controller, const QString& input)
{
    const QString cmd = input.toLower();
    if (cmd == QStringLiteral("1") || cmd == QStringLiteral("work")) {
        auto* wmc = controller.workModeController();
        if (wmc) wmc->requestWork();
        else qWarning() << QStringLiteral("工作模式控制器未就绪");
    } else if (cmd == QStringLiteral("2") || cmd == QStringLiteral("standby")) {
        auto* wmc = controller.workModeController();
        if (wmc) wmc->requestStandby();
        else qWarning() << QStringLiteral("工作模式控制器未就绪");
    } else if (cmd == QStringLiteral("h") || cmd == QStringLiteral("help")) {
        printUsage();
    } else if (cmd == QStringLiteral("q") || cmd == QStringLiteral("quit")) {
        qDebug() << QStringLiteral("程序退出...");
        QCoreApplication::quit();
    } else {
        qWarning() << QStringLiteral("未知命令: '%1'，输入 h 查看帮助").arg(cmd);
    }
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    configureWindowsConsoleEncoding();
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NewAntiDrone-core"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    Logger::instance().init();
    qInstallMessageHandler(Logger::messageHandler);
    qInfo() << QStringLiteral("[Logger] 日志文件: %1")
                   .arg(Logger::instance().currentLogFile());

    printBanner();


    AppController controller;
    if (!controller.initialize()) { return 1; } 
    controller.start();
    printUsage();


    auto ws = controller.startWebService();
    if (ws.ok) {
        qDebug() << QStringLiteral("Web UI 开发模式: http://localhost:5173");
        qDebug() << QStringLiteral("Web API: http://localhost:%1").arg(ws.httpPort);
    }

    // 命令行 stdin 输入
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::lineRead, &controller,
                     [&controller](const QString& input) {
        handleConsoleCommand(controller, input);
    });
    reader.start();

    // 优雅退出
#ifdef Q_OS_UNIX
    signal(SIGINT, [](int) { QCoreApplication::quit(); });
    signal(SIGTERM, [](int) { QCoreApplication::quit(); });
#endif
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&controller, &reader]() {
        controller.stop();
        reader.terminate();
        reader.wait(1000);
    });

    return app.exec();
}

