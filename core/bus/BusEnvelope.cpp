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

BusEnvelope BusEnvelope::makeTrackInteractionCommand(
    std::shared_ptr<const TrackInteractionCommand> cmd)
{
    static std::atomic<uint32_t> seq{0};
    BusEnvelope env;
    env.header.sourceType = SOURCE_WEB_API;
    env.header.dispatchTarget = DISPATCH_TRACK_DISPLAY_MANAGER;
    env.header.packetSequence = ++seq;
    env.header.timeStamp = cmd
        ? static_cast<uint64_t>(cmd->timestamp)
        : static_cast<uint64_t>(0);
    env.header.payloadType = PAYLOAD_TYPE_TRACK_INTERACTION_COMMAND;
    env.header.payloadSize = 0;
    env.payloadKind = BusPayloadKind::TrackInteractionCommand;
    env.objectPayload = std::move(cmd);
    return env;
}
