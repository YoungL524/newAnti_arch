#include "DesktopDeployment.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QLocalSocket>
#include <QMessageBox>
#include <QStandardPaths>

/**
 * @brief 以无控制台 Windows 子系统进入单机启动器，并在创建子进程前取得当前用户安装实例锁。
 * @details 重复启动只通知主实例，不移除存活实例的锁或 IPC；只有正常生命周期结束才退出事件循环。
 */
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NewAntiDroneLauncher"));
    app.setQuitOnLastWindowClosed(false);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (data.isEmpty() || !QDir().mkpath(data)) {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), QStringLiteral("无法创建启动器应用数据目录"));
        return 1;
    }
    const QByteArray identity = QCryptographicHash::hash(
        (QDir(app.applicationDirPath()).canonicalPath().toLower() + data.toLower()).toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    const QString channel = QStringLiteral("newanti-deployment-") + QString::fromLatin1(identity);
    QLockFile lock(QDir(data).filePath(QString::fromLatin1(identity) + QStringLiteral(".lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        QLocalSocket existing;
        existing.connectToServer(channel);
        if (existing.waitForConnected(3000)) {
            existing.write("OPEN\n");
            existing.waitForBytesWritten(1000);
            return 0;
        }
        QMessageBox::warning(nullptr, QStringLiteral("应用已运行"), QStringLiteral("已有启动器正在运行或退出，请使用系统托盘入口。"));
        return 1;
    }
    DesktopDeployment deployment(channel);
    QObject::connect(&deployment, &DesktopDeployment::finished, &app, [&app](int code) { app.exit(code); });
    QTimer::singleShot(0, &deployment, &DesktopDeployment::start);
    return app.exec();
}
