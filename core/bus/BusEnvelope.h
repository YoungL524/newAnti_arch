#pragma once

#include "GenericPackets.h"
#include "TrackTypes.h"
#include <atomic>
#include <memory>

/**
 * @brief 总线消息信封：固定 DataPacket 头 + 可选大型业务对象载荷
 * @details 小包仍走 DataPacket::payload.rawData；TrackDisplaySnapshot 等走 shared_ptr，
 *          避免塞进 MAX_PAYLOAD_SIZE 固定数组。
 */
struct BusEnvelope {
    DataPacket packet;
    std::shared_ptr<const TrackDisplaySnapshot> trackSnapshot;

    static BusEnvelope makeTrackDisplaySnapshot(std::shared_ptr<const TrackDisplaySnapshot> snap);

    /** @brief 总线 worker 分发线程内，从 DataPacket 取伴随 snapshot */
    static std::shared_ptr<const TrackDisplaySnapshot> trackSnapshotFrom(const DataPacket* pkt);

    static void setDispatchContext(const BusEnvelope* env);
    static void clearDispatchContext();

private:
    static thread_local const BusEnvelope* s_dispatching;
};

inline BusEnvelope BusEnvelope::makeTrackDisplaySnapshot(
    std::shared_ptr<const TrackDisplaySnapshot> snap)
{
    static std::atomic<uint32_t> seq{0};
    BusEnvelope env;
    env.packet.sourceType = SOURCE_TRACK_DISPLAY;
    env.packet.dispatchTarget = UI_OUTPUTWIDGET;
    env.packet.packetSequence = ++seq;
    env.packet.timeStamp = snap
        ? static_cast<uint64_t>(snap->timestamp)
        : static_cast<uint64_t>(0);
    env.packet.payloadType = PAYLOAD_TYPE_TRACK_DISPLAY_SNAPSHOT;
    env.packet.payloadSize = 0;
    env.trackSnapshot = std::move(snap);
    return env;
}

inline std::shared_ptr<const TrackDisplaySnapshot> BusEnvelope::trackSnapshotFrom(
    const DataPacket* pkt)
{
    if (!pkt || pkt->payloadType != PAYLOAD_TYPE_TRACK_DISPLAY_SNAPSHOT || !s_dispatching)
        return {};
    if (s_dispatching->packet.packetSequence != pkt->packetSequence
        || s_dispatching->packet.timeStamp != pkt->timeStamp)
        return {};
    return s_dispatching->trackSnapshot;
}
