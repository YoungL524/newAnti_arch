#include "BusEnvelope.h"
#include "TrackTypes.h"
#include <atomic>

BusEnvelope BusEnvelope::makeTrackDisplaySnapshot(
    std::shared_ptr<const TrackDisplaySnapshot> snap)
{
    static std::atomic<uint32_t> seq{0};
    BusEnvelope env;
    env.header.sourceType = SOURCE_TRACK_DISPLAY;
    env.header.dispatchTarget = UI_OUTPUTWIDGET;
    env.header.packetSequence = ++seq;
    env.header.timeStamp = snap
        ? static_cast<uint64_t>(snap->timestamp)
        : static_cast<uint64_t>(0);
    env.header.payloadType = PAYLOAD_TYPE_TRACK_DISPLAY_SNAPSHOT;
    env.header.payloadSize = 0;
    env.payloadKind = BusPayloadKind::TrackDisplaySnapshot;
    env.objectPayload = std::move(snap);
    return env;
}
