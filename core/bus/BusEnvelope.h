#pragma once

#include "GenericPackets.h"
#include <memory>

/**
 * @brief 总线对象载荷类型
 * @details DataPacket 仅承载路由头与小包 rawData；复杂/大对象通过 BusEnvelope::objectPayload 挂载。
 */
enum class BusPayloadKind : uint8_t {
    RawDataPacket = 0,
    TrackDisplaySnapshot,
};

/**
 * @brief 总线消息实体：路由头 + 可选共享只读对象载荷
 */
struct BusEnvelope {
    DataPacket header;
    BusPayloadKind payloadKind = BusPayloadKind::RawDataPacket;
    std::shared_ptr<const void> objectPayload;

    /** @brief 路由头别名，兼容旧代码 */
    const DataPacket& packet() const { return header; }
    DataPacket& packet() { return header; }

    template<typename T>
    std::shared_ptr<const T> payloadAs() const
    {
        return std::static_pointer_cast<const T>(objectPayload);
    }
};

struct TrackDisplaySnapshot;
BusEnvelope makeTrackDisplaySnapshot(std::shared_ptr<const TrackDisplaySnapshot> snap);
