/**
 * @file RadarControlSettingsJson.h
 * @brief 声明雷达控制参数在低频JSON边界上的唯一转换与校验入口。
 * @details 上游为HTTP对象或SQLite文本，下游为RadarControlSettings；转换不接触协议载荷、不发送命令，
 * 仅用于防止模板、任务快照和Web接口各自维护一套字段映射。
 */

#pragma once

#include "core/task/RadarControlSettings.h"

#include <QJsonObject>
#include <QString>

namespace core::task {

/**
 * @brief 校验完整控制参数是否满足冻结的V1值域和模式控制字。
 * @param settings 待校验的业务值。
 * @param error 失败时写入首个字段错误，成功时清空。
 * @return 全部字段合法返回true。
 */
bool validateRadarControlSettings(const RadarControlSettings &settings, QString &error);

/** @brief 把完整控制参数序列化成稳定字段名的JSON对象。 */
QJsonObject radarControlSettingsToJson(const RadarControlSettings &settings);

/**
 * @brief 从完整JSON对象解析并校验控制参数，禁止缺字段时静默继承旧值。
 * @param object HTTP或SQLite提供的完整对象。
 * @param settings 成功时写入新值，失败时保持调用前内容。
 * @param error 失败时写入字段级错误。
 * @return 结构、数值类型和值域均合法返回true。
 */
bool radarControlSettingsFromJson(
    const QJsonObject &object, RadarControlSettings &settings, QString &error);

/** @brief 返回V1默认控制参数的紧凑JSON文本，供Schema迁移和内置模板复用。 */
QString defaultRadarControlSettingsJson();

} // namespace core::task
