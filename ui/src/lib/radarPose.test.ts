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
    revision: 1,
    name: 'radar_1',
    brand: 'YHF',
    model: 'YHF-YZ-KU',
    protocolAdapterKey: 'dcmp.dp.v1',
    lng: 118,
    lat: 32,
    altitude: 100,
    maxRange: 5,
    faces: [{ label: 'A', heading: 0, pitch: 0, roll: 0, scanAngle: 45, active: true }],
    endpoint: {
      id: 'radar_cpu', protocol: 'udp', localAddress: '127.0.0.1', localPort: 7788,
      description: '测试端点', targets: {},
    },
    ...overrides,
  }
}

describe('radar 6DOF projection', () => {
  it('keeps zero-degree local azimuth pointing north', () => {
    const p = projectRadarTrack(radar(), 0, 0, 0, 1000)!
    expect(p.eastM).toBeCloseTo(0, 8)
    expect(p.northM).toBeCloseTo(1000, 8)
    expect(p.bearingDeg).toBeCloseTo(0, 8)
  })

  it('applies the sole active face absolute heading', () => {
    const device = radar({
      faces: [{ label: 'A', heading: 180, pitch: 0, roll: 0, scanAngle: 45, active: true }],
    })
    expect(trackAbsoluteHeading(device, 0)).toBe(180)
    expect(projectRadarTrack(device, 0, 0, 0, 1000)!.bearingDeg).toBeCloseTo(180, 8)
  })

  it('does not guess a face when multiple faces are active', () => {
    const device = radar({
      faces: [
        { label: 'A', heading: 0, pitch: 0, roll: 0, scanAngle: 45, active: true },
        { label: 'B', heading: 180, pitch: 5, roll: -5, scanAngle: 45, active: true },
      ],
    })
    expect(trackAbsoluteHeading(device, 1)).toBe(180)
    expect(projectRadarTrack(device, 1, 0, 0, 1000)!.bearingDeg).toBeCloseTo(180, 8)
  })

  it('projects positive pitch upward', () => {
    const device = radar({
      faces: [{ label: 'A', heading: 0, pitch: 30, roll: 0, scanAngle: 45, active: true }],
    })
    const p = projectRadarTrack(device, 0, 0, 0, 1000)!
    expect(p.northM).toBeCloseTo(1000 * Math.cos(Math.PI / 6), 8)
    expect(p.upM).toBeCloseTo(500, 8)
    expect(p.altitude).toBeCloseTo(600, 8)
  })

  it('projects a right-side target downward for positive roll', () => {
    const device = radar({
      faces: [{ label: 'A', heading: 0, pitch: 0, roll: 30, scanAngle: 45, active: true }],
    })
    const p = projectRadarTrack(device, 0, 90, 0, 1000)!
    expect(p.eastM).toBeCloseTo(1000 * Math.cos(Math.PI / 6), 8)
    expect(p.upM).toBeCloseTo(-500, 8)
  })

  it('uses a face heading directly', () => {
    expect(absoluteFaceHeading(180)).toBe(180)
  })

  it('rejects a track whose physical face is absent from the device revision', () => {
    expect(projectRadarTrack(radar(), 3, 0, 0, 1000)).toBeNull()
  })
})
