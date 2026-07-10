#pragma once

#include <QString>
#include <QJsonObject>
#include <QMap>

/**
 * @file ConfigManager.h
 * @brief 统一配置管理器
 * @details 数据访问层组件。负责定位配置目录、加载/保存 JSON 配置文件。
 *          取代各模块各自硬编码配置路径的做法，提供统一的配置访问入口。
 *
 * 使用方式：
 * @code
 *   ConfigManager cfg;
 *   cfg.loadConfigDir();                               // 自动搜索 config/ 目录
 *   QJsonObject dbCfg = cfg.getConfig("database");     // 加载 database.json
 *   QJsonObject epCfg = cfg.getConfig("endpoints");    // 加载 endpoints.json
 * @endcode
 *
 * 搜索顺序（首个存在的生效）：
 *   1. 工作目录/config/
 *   2. exe所在目录/config/
 *   3. exe上级目录/config/  （适配 build 子目录场景）
 */
class ConfigManager
{
public:
    ConfigManager() = default;

    /**
     * @brief 定位并加载配置目录
     * @param hint 可选的目录路径提示；为空时自动搜索
     * @return 是否成功找到配置目录
     */
    bool loadConfigDir(const QString& hint = QString());

    /**
     * @brief 获取已定位的配置目录绝对路径
     * @return 配置目录路径（未加载时返回空）
     */
    QString configDir() const { return m_configDir; }

    /**
     * @brief 按文件名加载 JSON 配置（不含扩展名）
     * @param name 配置名，例如 "database" 对应 config/database.json
     * @return QJsonObject；文件不存在或解析失败时返回空对象
     * @note 首次加载后会缓存，后续调用直接返回缓存
     */
    QJsonObject getConfig(const QString& name) const;

    /**
     * @brief 保存 JSON 配置到文件
     * @param name 配置名（不含扩展名）
     * @param config 要保存的 JSON 对象
     * @return 是否保存成功
     */
    bool saveConfig(const QString& name, const QJsonObject& config);

    /**
     * @brief 获取指定配置文件中的路径
     * @param configFilePath 配置文件的完整路径
     * @return 配置文件完整路径
     */
    QString configFilePath(const QString& name) const;

    /**
     * @brief 校验配置（启动时调用，非阻塞性：仅日志警告）
     * @param name 配置名（"endpoints" 或 "processor"）
     * @param obj  已加载的 JSON 对象
     * @return 错误消息列表（空=无异常）
     */
    static QStringList validateConfig(const QString& name, const QJsonObject& obj);

    /**
     * @brief 清除缓存，强制下次重新读取文件
     */
    void clearCache();

private:
    /**
     * @brief 从文件加载 JSON
     * @param filePath 文件完整路径
     * @return QJsonObject
     */
    QJsonObject loadJsonFile(const QString& filePath) const;

    QString m_configDir;                       ///< 配置目录绝对路径
    mutable QMap<QString, QJsonObject> m_cache;         ///< 已加载配置的缓存
};
