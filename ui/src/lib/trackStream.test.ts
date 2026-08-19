/**
 * @file trackStream.test.ts
 * @brief 验证 trackStream.test.ts 对应模块的验收契约并防止功能回归。
 * @details 测试覆盖可观察行为、边界条件与故障隔离；测试用例名称和断言共同构成验收标准。
 */

import { describe, expect, it } from 'vitest'
import type { WsTrackDisplayPoint, WsTracksMessage } from './protocol'
import { applyTrackStreamMessage, trackPointKey } from './trackStream'

const point = (pointIndex: number): WsTrackDisplayPoint => ({
  deviceId: 'radar-1', taskId: 'task-1', trackId: 9, pointIndex,
  timestamp: pointIndex, range: 100, azimuth: 0, elevation: 0,
  altitude: 0, radialSpeed: 0,
})

describe('track stream reducer', () => {
  it('replaces state on snapshot and applies ordered upserts/removals', () => {
    const points = new Map<string, WsTrackDisplayPoint>()
    const snapshot: WsTracksMessage = {
      type: 'tracks', mode: 'snapshot', revision: 10, timestamp: 1,
      tracks: [point(0), point(1)],
    }
    let revision = applyTrackStreamMessage(points, 0, snapshot)
    expect(revision).toBe(10)
    expect(points.size).toBe(2)

    const delta: WsTracksMessage = {
      type: 'tracks', mode: 'delta', revision: 11, timestamp: 2,
      removals: [{ deviceId: 'radar-1', trackId: 9, pointIndex: 0 }],
      upserts: [point(2)],
    }
    revision = applyTrackStreamMessage(points, revision!, delta)
    expect(revision).toBe(11)
    expect(points.has(trackPointKey(point(0)))).toBe(false)
    expect(points.has(trackPointKey(point(2)))).toBe(true)
  })

  it('rejects a revision gap without partially mutating state', () => {
    const existing = point(1)
    const points = new Map([[trackPointKey(existing), existing]])
    const gap: WsTracksMessage = {
      type: 'tracks', mode: 'delta', revision: 8, timestamp: 2,
      removals: [{ deviceId: 'radar-1', trackId: 9, pointIndex: 1 }],
      upserts: [point(2)],
    }
    expect(applyTrackStreamMessage(points, 5, gap)).toBeNull()
    expect([...points.values()]).toEqual([existing])
  })
})
