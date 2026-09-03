/**
 * @file DeviceConfiguration.h
 * @brief 定义 UI 配置边界与 SQLite devices revision 共用的设备配置值。
 * @details 上游 React/Qt API 将用户输入转换为本类型，DeviceConfigService 校验后交给 SQLite 持久化，下游
 * 设备注册表只提取紧凑 DeviceId/EndpointId 与安装姿态。本类型不进入 CoreDataDomain、不随每条航迹复制，
 * 也不解释尚未冻结的厂家协议；新人应先阅读 revision、endpointsJson 和绝对姿态字段。
 */

#pragma once

#include <QString>
#include <QUuid>
#include <QtGlobal>

namespace core::config {

/**
 * @brief 保存一个可审计且可形成不可变 SQLite revision 的完整设备配置。
 * @note QString/JSON 只存在于低频配置平面；实时 TrackPacket 只保存数值句柄，不复制本结构。
 */
struct DeviceConfiguration
{
    QUuid deviceUuid; ///< 跨重启稳定设备身份；新建设备时允许为空并由 Service 生成。
    quint32 revision = 0; ///< 调用保存时表示期望基版本；保存成功后改为新 revision。
    QString displayName; ///< 用户可见设备名称，不承担唯一身份。
    QString manufacturer; ///< 厂家名称；未知时为空。
    QString model; ///< 厂家型号；未知时为空。
    QString protocolAdapterKey; ///< 已登记协议适配器键；仅保存配置时可为空，开始采集前必须有效。
    double longitudeDeg = 0.0; ///< 雷达 RFU 原点经度，东经为正，范围 [-180,180]°。
    double latitudeDeg = 0.0; ///< 雷达 RFU 原点纬度，北纬为正，范围 [-90,90]°。
    double altitudeM = 0.0; ///< 相对平均海平面 MSL 的海拔高度，单位 m。
    double absoluteHeadingDeg = 0.0; ///< 真北为 0°、顺时针为正的绝对航向，范围 [0,360)°。
    double pitchDeg = 0.0; ///< 阵面抬头为正的俯仰角，范围 [-90,90]°。
    double rollDeg = 0.0; ///< 绕 RFU +Y、右侧下沉为正的横滚角，范围 [-180,180)°。
    QString endpointsJson = QStringLiteral("[]"); ///< 有界端点数组；每项具有 endpointKey/role/transport/地址端口。
    QString protocolConfigJson = QStringLiteral("{}"); ///< 尚未冻结的厂家私有配置对象，不进入实时数据链。
    QString changedBy; ///< 形成本 revision 的操作员或服务身份。
    QString changeReason; ///< 本次修改原因，允许为空但持久化保留。
};

} // namespace core::config
