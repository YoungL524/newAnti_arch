#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <csignal>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "AppController.h"
#include "StdinReader.h"
#include "HttpServer.h"
#include "log/Logger.h"
#include "RadarWorkModeController.h"

#ifdef Q_OS_WIN
/**
 * @brief 配置 Windows 控制台编码为 UTF-8
 */
static void configureWindowsConsoleEncoding()
{
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
}
#endif

/**
 * @brief 打印程序横幅
 */
static void printBanner()
{
    qDebug() << QStringLiteral("========================================");
    qDebug() << QStringLiteral("  NewAntiDrone-core v1.0.0");
    qDebug() << QStringLiteral("========================================");
}

/**
 * @brief 打印命令帮助
 */
static void printUsage();

/**
 * @brief 初始化并启动总控
 * @param controller 应用总控
 * @return 是否成功完成初始化与启动
 */
static bool initializeController(AppController& controller)
{
    if (!controller.initialize()) {
        qCritical() << QStringLiteral("总控初始化失败，程序退出");
        return false;
    }

    controller.start();
    return true;
}

/**
 * @brief 解析并执行控制台命令
 * @param controller 应用总控
 * @param input 原始输入
 */
static void handleConsoleCommand(AppController& controller, const QString& input)
{
    const QString cmd = input.toLower();
    if (cmd == QStringLiteral("1") || cmd == QStringLiteral("work")) {
        auto* wmc = controller.workModeController();
        if (wmc) { wmc->requestWork(); }
        else { qWarning() << QStringLiteral("工作模式控制器未就绪"); }
        return;
    }

    if (cmd == QStringLiteral("2") || cmd == QStringLiteral("standby")) {
        auto* wmc = controller.workModeController();
        if (wmc) { wmc->requestStandby(); }
        else { qWarning() << QStringLiteral("工作模式控制器未就绪"); }
        return;
    }

    if (cmd == QStringLiteral("h") || cmd == QStringLiteral("help")) {
        printUsage();
        return;
    }

    if (cmd == QStringLiteral("q") || cmd == QStringLiteral("quit")) {
        qDebug() << QStringLiteral("程序退出...");
        QCoreApplication::quit();
        return;
    }

    qWarning() << QStringLiteral("未知命令: '%1'，输入 h 查看帮助").arg(cmd);
}

/**
 * @brief 连接 stdin 读取线程与命令处理逻辑
 * @param reader stdin 读取线程
 * @param controller 应用总控
 */
static void connectCommandReader(StdinReader& reader, AppController& controller)
{
    QObject::connect(&reader, &StdinReader::lineRead, &controller,
                     [&controller](const QString& input) {
        handleConsoleCommand(controller, input);
    });
}

/**
 * @brief 安装 Unix 退出信号处理
 */
static void installUnixSignalHandlers()
{
#ifdef Q_OS_UNIX
    signal(SIGINT, [](int) { QCoreApplication::quit(); });
    signal(SIGTERM, [](int) { QCoreApplication::quit(); });
#endif
}

/**
 * @brief 连接应用退出清理逻辑
 * @param app Qt 应用对象
 * @param controller 应用总控
 * @param reader stdin 读取线程
 */
static void connectShutdownHandlers(QCoreApplication& app,
                                    AppController& controller,
                                    StdinReader& reader)
{
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&controller, &reader]() {
        controller.stop();
        reader.terminate();
        reader.wait(1000);
    });
}

/**
 * @brief 打印命令帮助
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

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Qt Creator 内置控制台中文支持
    configureWindowsConsoleEncoding();
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NewAntiDrone-core"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // 先初始化日志（依赖 QCoreApplication::applicationDirPath 定位 data/logs/）
    Logger::instance().init();
    qInstallMessageHandler(Logger::messageHandler);
    qInfo() << QStringLiteral("[Logger] 日志文件: %1")
                  .arg(Logger::instance().currentLogFile());

    printBanner();

    // 创建并初始化总控
    AppController controller;
    if (!initializeController(controller)) {
        return 1;
    }

    printUsage();

    // ---- HTTP + WebSocket 服务 ----
    HttpServer httpServer;
    auto wmc = controller.workModeController();

    // REST: GET /api/status
    httpServer.handleGet(QStringLiteral("/api/status"), [&controller, wmc](const QJsonObject &) {
        QJsonObject s;
        auto health = controller.healthReport();
        s[QStringLiteral("ok")] = true;
        s[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        s[QStringLiteral("uptime")] = QDateTime::currentSecsSinceEpoch();
        s[QStringLiteral("endpoints")] = health.comm.openEndpoints;
        s[QStringLiteral("dbConnected")] = health.database.connected;

        // 每设备状态数组
        QJsonArray devices;
        if (wmc) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
            dev[QStringLiteral("state")] = RadarWorkModeController::stateName(wmc->state());
            auto st = wmc->state();
            dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                               || st == RadarWorkModeController::State::Working);
            devices.append(dev);
        }
        s[QStringLiteral("devices")] = devices;

        // 向后兼容：全局聚合字段
        auto st2 = wmc ? wmc->state() : RadarWorkModeController::State::Standby;
        s[QStringLiteral("acquisition")] = (st2 == RadarWorkModeController::State::Starting
                                         || st2 == RadarWorkModeController::State::Working);
        s[QStringLiteral("state")] = wmc ? RadarWorkModeController::stateName(wmc->state()) : QStringLiteral("Unknown");

        return s;
    });

    // REST: POST /api/start (body 可选 { deviceIds: [...] })
    if (wmc) {
        httpServer.handlePost(QStringLiteral("/api/start"), [wmc](const QJsonObject &body) {
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            // 空数组或未传 = 控制全部设备
            bool shouldStart = ids.isEmpty();
            for (const auto &v : ids) {
                if (v.toString() == QStringLiteral("radar_cpu")) { shouldStart = true; break; }
            }
            if (shouldStart) wmc->requestWork();
            QJsonObject r;
            r[QStringLiteral("ok")] = true;
            return r;
        });

        // REST: POST /api/stop
        httpServer.handlePost(QStringLiteral("/api/stop"), [wmc](const QJsonObject &body) {
            QJsonArray ids = body[QStringLiteral("deviceIds")].toArray();
            bool shouldStop = ids.isEmpty();
            for (const auto &v : ids) {
                if (v.toString() == QStringLiteral("radar_cpu")) { shouldStop = true; break; }
            }
            if (shouldStop) wmc->requestStandby();
            QJsonObject r;
            r[QStringLiteral("ok")] = true;
            return r;
        });
    }

    // 输出 HTTP server 日志
    QObject::connect(&httpServer, &HttpServer::serverLog,
                     [](const QString &level, const QString &msg) {
        if (level == QStringLiteral("error"))
            qWarning().noquote() << QStringLiteral("[HTTP]") << msg;
        else
            qDebug().noquote() << QStringLiteral("[HTTP]") << msg;
    });

    // WebSocket 广播: 定时推送状态
    QTimer *statusTimer = new QTimer(&httpServer);
    QObject::connect(statusTimer, &QTimer::timeout, [&httpServer, &controller, wmc]() {
        auto health = controller.healthReport();
        QJsonObject msg;
        msg[QStringLiteral("type")] = QStringLiteral("status");
        msg[QStringLiteral("connected")] = health.comm.openEndpoints > 0;
        msg[QStringLiteral("upTime")] = health.timestampMs;

        QJsonArray devices;
        if (wmc) {
            QJsonObject dev;
            dev[QStringLiteral("id")] = QStringLiteral("radar_cpu");
            dev[QStringLiteral("state")] = RadarWorkModeController::stateName(wmc->state());
            auto st = wmc->state();
            dev[QStringLiteral("acquisition")] = (st == RadarWorkModeController::State::Starting
                                               || st == RadarWorkModeController::State::Working);
            devices.append(dev);
        }
        msg[QStringLiteral("devices")] = devices;
        auto st3 = wmc ? wmc->state() : RadarWorkModeController::State::Standby;
        msg[QStringLiteral("acquisition")] = (st3 == RadarWorkModeController::State::Starting
                                           || st3 == RadarWorkModeController::State::Working);
        msg[QStringLiteral("state")] = wmc ? RadarWorkModeController::stateName(wmc->state()) : QStringLiteral("Unknown");

        httpServer.broadcast(msg);
    });
    statusTimer->start(1000);

    // WebSocket 广播: 状态变化时立即推送
    if (wmc) {
        QObject::connect(wmc, &RadarWorkModeController::stateChanged,
                         &httpServer, [&httpServer](auto, auto) {
            // 状态变化由定时器在下一秒推送, 避免重复
        });
    }

    // 订阅 SystemBus → WebSocket 推送航迹数据
    httpServer.subscribeToBus();

    // 启动 HTTP 服务
    if (!httpServer.start(8088, 8089)) {
        qWarning() << QStringLiteral("HTTP 服务启动失败, 前端不可用");
    } else {
        // 自动打开浏览器
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://localhost:8088")));
    }

    // 命令行输入：独立线程读取 stdin，避免阻塞 Qt 事件循环
    StdinReader reader;
    connectCommandReader(reader, controller);
    reader.start();

    // 优雅退出
    installUnixSignalHandlers();
    connectShutdownHandlers(app, controller, reader);

    return app.exec();
}
