#include <QCoreApplication>
#include <QDebug>
#include "TestComm.h"
#include "StdinReader.h"
#include "Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief 打印命令帮助
 */
static void printUsage()
{
    qDebug() << QStringLiteral("\n========================================");
    qDebug() << QStringLiteral("  反无雷达测试工具（雷达端模拟器） v2.0");
    qDebug() << QStringLiteral("========================================");
    qDebug() << QStringLiteral("业务说明:");
    qDebug() << QStringLiteral("  - 接收 core 每 50ms 发来的 DPH，自动统计");
    qDebug() << QStringLiteral("  - 接收 core 的 workmode 命令：");
    qDebug() << QStringLiteral("      work    → 开始周期性上报航迹（默认 100ms）");
    qDebug() << QStringLiteral("      standby → 停止上报");
    qDebug() << QStringLiteral("  - 本端不主动发送命令（由 core 驱动）");
    qDebug() << QStringLiteral("----------------------------------------");
    qDebug() << QStringLiteral("命令列表:");
    qDebug() << QStringLiteral("  1 / dph        - 显示 DPH 接收统计");
    qDebug() << QStringLiteral("  2 / status     - 显示当前工作/上报状态");
    qDebug() << QStringLiteral("  3 / verbose    - 开关 DPH 详细打印");
    qDebug() << QStringLiteral("  h / help       - 显示帮助");
    qDebug() << QStringLiteral("  q / quit       - 退出程序");
    qDebug() << QStringLiteral("========================================\n");
}

int main(int argc, char *argv[])
{
    #ifdef Q_OS_WIN
        // 强制控制台代码页为 UTF-8，解决 Qt Creator 内置控制台中文乱码
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
    #endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_comm"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0"));

    Logger::instance().init();
    qInstallMessageHandler(Logger::messageHandler);
    qInfo() << QStringLiteral("[Logger] 日志文件: %1")
                   .arg(Logger::instance().currentLogFile());

    qDebug() << QStringLiteral("反无雷达通信测试工具启动...");
    
    // 命令行参数：目标IP 目标端口 workmode端口 DPH端口
    QString targetAddress = QStringLiteral("127.0.0.1");
    quint16 targetPort    = 7788;   // core 监听端口
    quint16 workmodePort  = 1234;   // 本端监听 workmode 的端口
    quint16 dphPort       = 3456;   // 本端监听 DPH 的端口

    if (argc > 1) targetAddress = QString::fromLocal8Bit(argv[1]);
    if (argc > 2) targetPort    = static_cast<quint16>(QString::fromLocal8Bit(argv[2]).toUInt());
    if (argc > 3) workmodePort  = static_cast<quint16>(QString::fromLocal8Bit(argv[3]).toUInt());
    if (argc > 4) dphPort       = static_cast<quint16>(QString::fromLocal8Bit(argv[4]).toUInt());

    TestComm testComm;
    testComm.setTarget(targetAddress, targetPort);

    if (!testComm.initialize(workmodePort, dphPort)) {
        qCritical() << QStringLiteral("初始化失败，程序退出");
        return 1;
    }
    
    printUsage();
    
    // 启动 stdin 读取线程（不阻塞事件循环）
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::lineRead, &testComm,
                     [&](const QString& input) {
        const QString cmd = input.toLower();

        if (cmd == QStringLiteral("1") || cmd == QStringLiteral("dph")) {
            testComm.printDphStats();
        }
        else if (cmd == QStringLiteral("2") || cmd == QStringLiteral("status")) {
            qDebug() << QStringLiteral("目标(core): %1:%2, 本地监听: workmode=%3, DPH=%4")
                        .arg(targetAddress).arg(targetPort)
                        .arg(workmodePort).arg(dphPort);
            testComm.printStatus();
            testComm.printDphStats();
        }
        else if (cmd == QStringLiteral("3") || cmd == QStringLiteral("verbose")) {
            testComm.toggleVerbose();
        }
        else if (cmd == QStringLiteral("h") || cmd == QStringLiteral("help")) {
            printUsage();
        }
        else if (cmd == QStringLiteral("q") || cmd == QStringLiteral("quit")) {
            qDebug() << QStringLiteral("程序退出...");
            QCoreApplication::quit();
        }
        else {
            qWarning() << QStringLiteral("未知命令: '%1'，输入 h 查看帮助").arg(cmd);
        }
    });
    reader.start();
    
    // 退出时停止读取线程
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        reader.terminate();
        reader.wait(1000);
    });
    
    return app.exec();
}


