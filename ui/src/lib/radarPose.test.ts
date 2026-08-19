/**
 * @file radarPose.test.ts
 * @brief 验证 radarPose.test.ts 对应模块的验收契约并防止功能回归。
 * @details 测试覆盖可观察行为、边界条件与故障隔离；测试用例名称和断言共同构成验收标准。
 */

import { describe, expect, it } from 'vitest'
import type { RadarDevice } from '../components/map/types'
import { absoluteFaceHeading, projectRadarTrack, trackAbsoluteHeading } from './radarPose'

function radar(overrides: Partial<RadarDevice> = {}): RadarDevice {
  return {
    id: 'radar_1',
    name: 'radar_1',
    brand: 'YHF',
    lng: 118,
    lat: 32,
    altitude: 100,
    pitch: 0,
    roll: 0,
    maxRange: 5,
    faces: [{ label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true }],
    ...overrides,
  }
}

describe('radar 6DOF projection', () => {
  it('keeps zero-degree local azimuth pointing north', () => {
    const p = projectRadarTrack(radar(), 0, 0, 1000)
    expect(p.eastM).toBeCloseTo(0, 8)
    expect(p.northM).toBeCloseTo(1000, 8)
    expect(p.bearingDeg).toBeCloseTo(0, 8)
  })

  it('applies the sole active face absolute heading', () => {
    const device = radar({
      faces: [{ label: 'A', boresightAzimuth: 180, scanAngle: 45, active: true }],
    })
    expect(trackAbsoluteHeading(device)).toBe(180)
    expect(projectRadarTrack(device, 0, 0, 1000).bearingDeg).toBeCloseTo(180, 8)
  })

  it('does not guess a face when multiple faces are active', () => {
    const device = radar({
      faces: [
        { label: 'A', boresightAzimuth: 0, scanAngle: 45, active: true },
        { label: 'B', boresightAzimuth: 180, scanAngle: 45, active: true },
      ],
    })
    expect(trackAbsoluteHeading(device)).toBe(0)
  })

  it('projects positive pitch upward', () => {
    const p = projectRadarTrack(radar({ pitch: 30 }), 0, 0, 1000)
    expect(p.northM).toBeCloseTo(1000 * Math.cos(Math.PI / 6), 8)
    expect(p.upM).toBeCloseTo(500, 8)
    expect(p.altitude).toBeCloseTo(600, 8)
  })

  it('projects a right-side target downward for positive roll', () => {
    const p = projectRadarTrack(radar({ roll: 30 }), 90, 0, 1000)
    expect(p.eastM).toBeCloseTo(1000 * Math.cos(Math.PI / 6), 8)
    expect(p.upM).toBeCloseTo(-500, 8)
  })

  it('uses a face heading directly', () => {
    expect(absoluteFaceHeading(180)).toBe(180)
  })
})
