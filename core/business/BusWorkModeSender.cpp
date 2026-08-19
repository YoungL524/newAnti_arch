#include "BusWorkModeSender.h"

#include "GenericPackets.h"
#include "SystemBus.h"
#include "StructureReceive.h"

#include <QDateTime>
#include <QDebug>

#include <cstring>

/**
 * @brief 将工作/待机模式编码为总线 DataPacket 并发布给目标雷达设备。
 * @param deviceId 目标设备标识，用于总线路由选择。
 * @param mode 协议定义的工作待机值，当前 1 表示工作、0 表示待机。
 * @return 载荷装入数据包且总线接受发布时为 true；编码或发布失败时为 false。
 * @details 本机经纬高字段目前保持与旧链路一致的零值占位，后续应由 RuntimeSettings 的稳定快照提供。
 */
bool BusWorkModeSender::sendWorkMode(uint16_t deviceId, uint32_t mode)
{
    workStandby cmd;
    std::memset(&cmd, 0, sizeof(workStandby));
    cmd.Work_Standby_Mode = mode;
    // 本机位置占位：与原 AppController::sendWorkModeCommand 行为保持一致
    // 后续由 RuntimeSettings 业务对象提供真实快照
    cmd.Lon_Coordinates = 116.3974;
    cmd.Lat_Coordinates = 39.9093;
    cmd.Altitude = 100.0;

    DataPacket packet;
    packet.sourceType = UI_CONFIGWIDGET;
    packet.dispatchTarget = deviceId;
    packet.packetSequence = static_cast<uint32_t>(SystemBus::getInstance().getPublishedCount() + 1);
    packet.timeStamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());

    if (!packDataToPayload(packet, cmd)) {
        qWarning().noquote() << QStringLiteral(
            "[BusWorkModeSender] workStandby 打包失败 device=%1 mode=%2")
            .arg(deviceId).arg(mode);
        return false;
    }

    const bool ok = SystemBus::getInstance().publish(std::move(packet));
    if (!ok) {
        qWarning().noquote() << QStringLiteral(
            "[BusWorkModeSender] 发布到 SystemBus 失败 device=%1 mode=%2")
            .arg(deviceId).arg(mode);
    }
    return ok;
}
