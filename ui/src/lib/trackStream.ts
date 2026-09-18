/**
 * @file trackStream.ts
 * @brief 实现航迹快照、增量更新和删除消息的纯状态归并算法。
 * @details 以稳定航迹键和服务端 revision 维护有界当前态；检测到版本缺口时拒绝继续套用增量并要求重新获取快照。
 */

import type { WsTrackDisplayPoint, WsTrackDisplayPointKey, WsTracksMessage } from './protocol'

/**
 * @brief 生成旧 UI 显示点在一次 WebSocket 当前态中的归并键。
 * @param point 服务端显示点或删除键。
 * @return 由采集会话、整机、阵面、厂家航迹号和轨迹点序号组成的字符串键。
 */
export function trackPointKey(point: WsTrackDisplayPoint | WsTrackDisplayPointKey): string {
  return `${point.acquisitionId}:${point.deviceId}:${point.faceIndex}:${point.trackId}:${point.pointIndex}`
}

/**
 * @brief 将一条有序快照或增量消息归并到浏览器当前显示态。
 * @param points 当前 WebSocket 会话内唯一的显示点索引。
 * @param currentRevision 已成功应用的服务端修订号；尚未收到快照时为null。
 * @param message 本次收到的完整跨进程消息。
 * @return 新修订号；增量存在revision gap或修订号非法时返回null，调用方必须重连取得新快照。
 */
export function applyTrackStreamMessage(
  points: Map<string, WsTrackDisplayPoint>,
  currentRevision: string | null,
  message: WsTracksMessage,
): string | null {
  if (message.type === 'trackSnapshot') {
    points.clear()
    for (const point of message.payload.tracks) points.set(trackPointKey(point), point)
    return message.revision
  }

  if (currentRevision == null || !/^\d+$/.test(currentRevision) || !/^\d+$/.test(message.revision)) return null
  if (BigInt(message.revision) !== BigInt(currentRevision) + 1n) return null
  for (const key of message.payload.removals) points.delete(trackPointKey(key))
  for (const point of message.payload.upserts) points.set(trackPointKey(point), point)
  return message.revision
}
