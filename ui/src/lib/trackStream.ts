/**
 * @file trackStream.ts
 * @brief 实现航迹快照、增量更新和删除消息的纯状态归并算法。
 * @details 以稳定航迹键和服务端 revision 维护有界当前态；检测到版本缺口时拒绝继续套用增量并要求重新获取快照。
 */

import type { WsTrackDisplayPoint, WsTrackDisplayPointKey, WsTracksMessage } from './protocol'

export function trackPointKey(point: WsTrackDisplayPoint | WsTrackDisplayPointKey): string {
  return `${point.deviceId}:${point.trackId}:${point.pointIndex}`
}

/** Applies one ordered server message. Returns the new revision, or null on a delta gap. */
export function applyTrackStreamMessage(
  points: Map<string, WsTrackDisplayPoint>,
  currentRevision: number,
  message: WsTracksMessage,
): number | null {
  if (message.mode === 'snapshot') {
    points.clear()
    for (const point of message.tracks) points.set(trackPointKey(point), point)
    return message.revision
  }

  if (message.revision !== currentRevision + 1) return null
  for (const key of message.removals) points.delete(trackPointKey(key))
  for (const point of message.upserts) points.set(trackPointKey(point), point)
  return message.revision
}
