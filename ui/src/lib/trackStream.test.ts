/**
 * @file trackStream.test.ts
 * @brief 验证 WebReader 快照、增量、删除和采集会话隔离的浏览器归并合同。
 * @details 夹具使用同一 acquisitionId 构造一段尾迹，并断言 revision gap 不会部分修改当前显示态。
 */

import { describe, expect, it } from 'vitest'
import type { WsTrackDisplayPoint, WsTracksMessage } from './protocol'
import { applyTrackStreamMessage, trackPointKey } from './trackStream'

const point = (pointIndex: number): WsTrackDisplayPoint => ({
  acquisitionId: 'acquisition-1', deviceId: 'radar-1', faceIndex: 0, trackId: 9, pointIndex,
  receiveUtcMs: pointIndex, rangeM: 100, azimuthRad: 0, elevationRad: 0,
  radialVelocityMps: 0, amplitude: 0,
})

describe('track stream reducer', () => {
  it('replaces state on snapshot and applies ordered upserts/removals', () => {
    const points = new Map<string, WsTrackDisplayPoint>()
    const snapshot: WsTracksMessage = {
      type: 'trackSnapshot', revision: '10', sentUtcMs: 1,
      payload: { tracks: [point(0), point(1)] },
    }
    let revision = applyTrackStreamMessage(points, null, snapshot)
    expect(revision).toBe('10')
    expect(points.size).toBe(2)

    const delta: WsTracksMessage = {
      type: 'trackDelta', revision: '11', sentUtcMs: 2,
      payload: {
        removals: [{ acquisitionId: 'acquisition-1', deviceId: 'radar-1', faceIndex: 0, trackId: 9, pointIndex: 0 }],
        upserts: [point(2)],
      },
    }
    revision = applyTrackStreamMessage(points, revision!, delta)
    expect(revision).toBe('11')
    expect(points.has(trackPointKey(point(0)))).toBe(false)
    expect(points.has(trackPointKey(point(2)))).toBe(true)
  })

  it('rejects a revision gap without partially mutating state', () => {
    const existing = point(1)
    const points = new Map([[trackPointKey(existing), existing]])
    const gap: WsTracksMessage = {
      type: 'trackDelta', revision: '8', sentUtcMs: 2,
      payload: {
        removals: [{ acquisitionId: 'acquisition-1', deviceId: 'radar-1', faceIndex: 0, trackId: 9, pointIndex: 1 }],
        upserts: [point(2)],
      },
    }
    expect(applyTrackStreamMessage(points, '5', gap)).toBeNull()
    expect([...points.values()]).toEqual([existing])
  })
})
