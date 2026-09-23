/**
 * @file DeviceConfiguration.h
 * @brief 定义 UI 配置边界与 SQLite devices revision 共用的设备配置值。
 * @details 上游 React/Qt API 将用户输入转换为本类型，DeviceConfigService 校验后交给 SQLite 持久化，下游
 * 设备注册表只提取紧凑 DeviceId/EndpointId 与阵面安装姿态。本类型不进入 CoreDataDomain、不随每条航迹复制，
 * 也不解释厂家运行控制参数；新人应先阅读 revision、facesJson 和 endpointsJson。
 */

#pragma once

#include <QString>
#include <QUuid>
#include <QtGlobal>

namespace core::config {

/** @brief 表示主链设备台账允许登记的完整雷达阵面形态。 */
enum class RadarArrayType : quint8
{
    SingleFace = 1, ///< 单面阵整机，facesJson 必须只含0号阵面。
    FourFace = 4 ///< 四面阵整机，facesJson 必须完整包含0～3号阵面。
};

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
    RadarArrayType arrayType = RadarArrayType::SingleFace; ///< 主链只接受单面阵或完整四面阵整机。
    double maxRangeM = 5000.0; ///< UI覆盖绘制使用的最大斜距，单位m，必须有限且大于零。
    QString facesJson = QStringLiteral("[{\"faceIndex\":0,\"headingDeg\":0,\"pitchDeg\":0,\"rollDeg\":0,\"scanHalfAngleDeg\":45,\"enabled\":true}]"); ///< 固定1或4项阵面安装姿态；航向为真北顺时针绝对角，不进入每条航迹。
    double longitudeDeg = 0.0; ///< 雷达 RFU 原点经度，东经为正，范围 [-180,180]°。
    double latitudeDeg = 0.0; ///< 雷达 RFU 原点纬度，北纬为正，范围 [-90,90]°。
    double altitudeM = 0.0; ///< 相对平均海平面 MSL 的海拔高度，单位 m。
    QString endpointsJson = QStringLiteral("[]"); ///< 有界端点数组；每项具有 endpointKey/role/transport/地址端口。
    QString protocolConfigJson = QStringLiteral("{}"); ///< 尚未冻结的厂家私有配置对象，不进入实时数据链。
    QString changedBy; ///< 形成本 revision 的操作员或服务身份。
    QString changeReason; ///< 本次修改原因，允许为空但持久化保留。
};

} // namespace core::config
