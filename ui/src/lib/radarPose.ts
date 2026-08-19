/**
 * @file radarPose.ts
 * @brief 提供雷达阵面绝对朝向的归一化和显示辅助函数。
 * @details 输入航向以正北 0°、顺时针为正；只归一化到 `[0°,360°)`，不与其他姿态字段相加。
 */

import type { RadarDevice } from '../components/map/types'

export interface RadarProjection {
  lng: number
  lat: number
  altitude: number
  bearingDeg: number
  eastM: number
  northM: number
  upM: number
}

const DEG_TO_RAD = Math.PI / 180
const METERS_PER_DEG_LAT = 111320

export function trackAbsoluteHeading(device: RadarDevice): number {
  const activeFaces = device.faces.filter(face => face.active)
  return activeFaces.length === 1 ? activeFaces[0].boresightAzimuth : 0
}

/**
 * Converts radar-local polar coordinates to WGS-84 display coordinates.
 * Conventions: azimuth 0°=forward/north and clockwise; elevation positive up;
 * heading is an absolute true-north angle in [0°, 360°), clockwise positive;
 * pitch is nose-up positive; roll is right-side-down positive.
 * With exactly one active face, that face's measured absolute heading is used.
 * Otherwise no heading correction is applied because the current track
 * protocol does not carry a face identifier. Headings are never added.
 */
export function projectRadarTrack(
  device: RadarDevice,
  azimuthDeg: number,
  elevationDeg: number,
  slantRangeM: number,
): RadarProjection {
  const azimuth = azimuthDeg * DEG_TO_RAD
  const elevation = elevationDeg * DEG_TO_RAD
  const horizontal = slantRangeM * Math.cos(elevation)

  // Local body axes: x=right, y=forward, z=up.
  let x = horizontal * Math.sin(azimuth)
  let y = horizontal * Math.cos(azimuth)
  let z = slantRangeM * Math.sin(elevation)

  // Roll about forward axis (right side down is positive).
  const roll = device.roll * DEG_TO_RAD
  ;[x, z] = [x * Math.cos(roll) + z * Math.sin(roll),
             -x * Math.sin(roll) + z * Math.cos(roll)]

  // Pitch about right axis (nose up is positive).
  const pitch = device.pitch * DEG_TO_RAD
  ;[y, z] = [y * Math.cos(pitch) - z * Math.sin(pitch),
             y * Math.sin(pitch) + z * Math.cos(pitch)]

  // Rotate by the measured absolute heading (clockwise from north).
  const heading = trackAbsoluteHeading(device) * DEG_TO_RAD
  const eastM = x * Math.cos(heading) + y * Math.sin(heading)
  const northM = -x * Math.sin(heading) + y * Math.cos(heading)
  const upM = z
  const metersPerDegLng = METERS_PER_DEG_LAT * Math.cos(device.lat * DEG_TO_RAD)

  return {
    lng: device.lng + eastM / metersPerDegLng,
    lat: device.lat + northM / METERS_PER_DEG_LAT,
    altitude: device.altitude + upM,
    bearingDeg: ((Math.atan2(eastM, northM) / DEG_TO_RAD) + 360) % 360,
    eastM,
    northM,
    upM,
  }
}

export function absoluteFaceHeading(faceAzimuthDeg: number): number {
  return ((faceAzimuthDeg % 360) + 360) % 360
}
