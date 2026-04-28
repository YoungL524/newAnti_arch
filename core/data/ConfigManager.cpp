#include "ConfigManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

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

QJsonObject ConfigManager::getConfig(const QString& name)
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

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << QStringLiteral("[ConfigManager] 无法写入: %1").arg(filePath);
        return false;
    }

    QJsonDocument doc(config);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

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
