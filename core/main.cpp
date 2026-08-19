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
/**
 * @brief 将 Windows 控制台的输入、输出代码页切换为 UTF-8。
 * @details 必须在首次输出 Qt 日志之前调用，否则中文横幅和诊断信息可能按系统本地代码页显示为乱码。
 */
static void configureWindowsConsoleEncoding()
{
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}
#endif

/**
 * @brief 输出核心进程名称和版本横幅。
 * @details 横幅经 Qt 日志系统输出，因此既出现在控制台，也进入当日日志文件。
 */
static void printBanner()
{
    qDebug() << QStringLiteral("========================================");
    qDebug() << QStringLiteral("  NewAntiDrone-core v1.0.0");
    qDebug() << QStringLiteral("========================================");
}

/**
 * @brief 输出雷达工作模式的命令行操作说明。
 * @details 列出工作、待机、帮助和退出命令，并说明 DPH 周期发送与雷达航迹上报之间的关系。
 */
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

/**
 * @brief 将一行控制台输入转换为工作模式命令或进程控制命令。
 * @param controller 已完成初始化的应用控制器，用于取得工作模式控制器。
 * @param input 已去除首尾空白的命令文本；命令名称不区分大小写。
 * @details `work`、`standby` 只发起模式请求，不直接操作通信端点；控制器未就绪时记录告警。
 *          `quit` 通过 Qt 事件循环发起退出，以便 `aboutToQuit` 回调完成有序停机。
 */
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

/**
 * @brief 组装并运行 NewAntiDrone 核心进程。
 * @param argc 命令行参数个数。
 * @param argv 命令行参数数组。
 * @return 事件循环退出码；应用控制器初始化失败时返回 1。
 * @details 启动顺序为日志系统、核心控制器、Web 服务和标准输入线程。退出时先停止核心运行时，
 *          再终止并等待输入线程，避免通信、定时任务或后台输入在对象析构期间继续访问资源。
 */
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
