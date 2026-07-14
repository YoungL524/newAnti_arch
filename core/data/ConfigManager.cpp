#include "ConfigManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <QSaveFile>

bool ConfigManager::loadConfigDir(const QString& hint)
{
    // 如果提供了明确路径，直接使用
    if (!hint.isEmpty()) {
        QDir dir(hint);
        if (dir.exists()) {
            m_configDir = dir.absolutePath();
            qDebug() << QStringLiteral("[ConfigManager] 配置目录（指定）: %1").arg(m_configDir);
            return true;
        }
        qWarning() << QStringLiteral("[ConfigManager] 指定的配置目录不存在: %1").arg(hint);
    }

    // 自动搜索：按优先级尝试多个候选路径
    QString exeDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        QDir::currentPath() + QStringLiteral("/config"),
        exeDir + QStringLiteral("/config"),
        exeDir + QStringLiteral("/../config")
    };

    for (const QString& path : candidates) {
        QDir dir(path);
        if (dir.exists()) {
            m_configDir = dir.absolutePath();
            qDebug() << QStringLiteral("[ConfigManager] 配置目录: %1").arg(m_configDir);
            return true;
        }
    }

    qCritical() << QStringLiteral("[ConfigManager] 未找到 config/ 目录，搜索路径:");
    for (const QString& path : candidates) {
        qCritical() << QStringLiteral("  - %1").arg(path);
    }
    return false;
}

QJsonObject ConfigManager::getConfig(const QString& name) const
{
    // 检查缓存
    auto it = m_cache.constFind(name);
    if (it != m_cache.constEnd()) {
        return it.value();
    }

    // 从文件加载
    QString filePath = configFilePath(name);
    QJsonObject obj = loadJsonFile(filePath);

    if (!obj.isEmpty()) {
        m_cache.insert(name, obj);
    }

    return obj;
}

bool ConfigManager::saveConfig(const QString& name, const QJsonObject& config)
{
    QString filePath = configFilePath(name);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << QStringLiteral("[ConfigManager] 无法写入: %1").arg(filePath);
        return false;
    }

    QJsonDocument doc(config);
    const QByteArray content = doc.toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size() || !file.commit()) {
        qWarning() << QStringLiteral("[ConfigManager] 原子写入失败: %1 - %2")
                         .arg(filePath, file.errorString());
        return false;
    }

    // 更新缓存
    m_cache.insert(name, config);

    qDebug() << QStringLiteral("[ConfigManager] 已保存配置: %1").arg(filePath);
    return true;
}

QString ConfigManager::configFilePath(const QString& name) const
{
    if (m_configDir.isEmpty()) {
        qWarning() << QStringLiteral("[ConfigManager] 配置目录未初始化");
        return QString();
    }
    return m_configDir + QStringLiteral("/") + name + QStringLiteral(".json");
}

void ConfigManager::clearCache()
{
    m_cache.clear();
}

QJsonObject ConfigManager::loadJsonFile(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return QJsonObject();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << QStringLiteral("[ConfigManager] 无法打开: %1").arg(filePath);
        return QJsonObject();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << QStringLiteral("[ConfigManager] JSON 解析错误 (%1): %2")
                      .arg(filePath, parseError.errorString());
        return QJsonObject();
    }

    qDebug() << QStringLiteral("[ConfigManager] 已加载: %1").arg(filePath);
    return doc.object();
}

QStringList ConfigManager::validateConfig(const QString& name, const QJsonObject& obj)
{
    QStringList errors;

    if (name == QStringLiteral("endpoints")) {
        QJsonArray eps = obj[QStringLiteral("endpoints")].toArray();
        if (eps.isEmpty()) {
            errors << QStringLiteral("endpoints.json: endpoints 数组为空");
        }
        for (int i = 0; i < eps.size(); ++i) {
            QJsonObject ep = eps[i].toObject();
            QString id = ep[QStringLiteral("id")].toString();
            int port = ep[QStringLiteral("localPort")].toInt();
            if (id.isEmpty())
                errors << QStringLiteral("endpoints.json[%1]: id 为空").arg(i);
            if (port <= 0)
                errors << QStringLiteral("endpoints.json[%1](%2): localPort <= 0").arg(i).arg(id);
        }
    } else if (name == QStringLiteral("processor")) {
        int target = obj[QStringLiteral("reverseTargetDevice")].toInt(0);
        if (target <= 0)
            errors << QStringLiteral("processor.json: reverseTargetDevice <= 0，反向流路由无法工作");
        int dphIvl = obj[QStringLiteral("dphPublisher")].toObject()[QStringLiteral("intervalMs")].toInt(0);
        if (dphIvl < 10 || dphIvl > 1000)
            errors << QStringLiteral("processor.json: dphPublisher.intervalMs 应在 10-1000 之间，当前为 %1").arg(dphIvl);
    }

    return errors;
}
