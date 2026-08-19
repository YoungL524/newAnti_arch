#include "TrackRecognizer.h"
#include "metrics/MonotonicClock.h"

/**
 * @brief 仅把 Pending 航迹推进为 Completed，并记录识别阶段完成的单调时间。
 * @details 非 Pending 对象原样拒绝，防止重复识别覆盖已有结果。完成时间取当前时刻和 normalizedSteadyNs
 *          的较大值，保证阶段时间始终非递减。
 */
bool TrackRecognizer::recognize(TrackPacket& packet) const
{
    if (packet.recognitionStatus != RecognitionStatus::Pending)
        return false;

    const qint64 nowNs = monotonicNowNs();
    packet.recognitionStatus = RecognitionStatus::Completed;
    packet.recognitionType = RecognitionType::Unknown;
    packet.recognizedSteadyNs = qMax<qint64>(nowNs, packet.normalizedSteadyNs);

    return true;
}
