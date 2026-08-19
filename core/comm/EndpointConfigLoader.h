#pragma once
/**
 * @file EndpointConfigLoader.h
 * @brief 在 endpoints.json 与协议无关的 EndpointConfig 之间执行纯结构转换。
 * @details 磁盘启动路径和 Web 热重载路径共用同一组转换函数，避免两套字段默认值。该类不拥有配置、
 *          不创建 Endpoint，也不执行端口占用或设备绑定等业务校验。
 */

#include "CommTypes.h"
#include <QJsonObject>
#include <QVector>
#include <QString>

/** @brief 无状态的端点配置序列化工具。 */
class EndpointConfigLoader
{
public:
    /** @brief 读取并解析文件；文件或 JSON 无效时记录日志并返回空集合。 */
    static QVector<EndpointConfig> loadFromJson(const QString& filePath);
    /** @brief 以完整集合覆盖写入缩进 JSON；无法打开目标文件时返回 false。 */
    static bool saveToJson(const QString& filePath, const QVector<EndpointConfig>& configs);
    /** @brief 从内存根对象读取 endpoints 数组，不执行文件输入输出。 */
    static QVector<EndpointConfig> parseFromJsonObject(const QJsonObject& root);
    /** @brief 生成包含 endpoints 数组的根对象。 */
    static QJsonObject toJsonObject(const QVector<EndpointConfig>& configs);
    /** @brief 转换单个端点对象并应用缺省字段。 */
    static EndpointConfig parseEndpoint(const QJsonObject& obj);
    /** @brief 把单个 EndpointConfig 转为 endpoints 数组元素。 */
    static QJsonObject endpointToJson(const EndpointConfig& cfg);
};
