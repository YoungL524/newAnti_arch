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

/**
 * @brief 按航迹携带的物理阵面编号读取唯一安装航向。
 * @param device 当前任务绑定的雷达台账revision。
 * @param faceIndex 协议中的0～3阵面索引。
 * @return 对应阵面的绝对航向；索引无效时返回null并由调用方丢弃该显示点。
 */
export function trackAbsoluteHeading(device: RadarDevice, faceIndex: number): number | null {
  const face = device.faces[faceIndex]
  return face ? absoluteFaceHeading(face.heading) : null
}

/**
 * @brief 将雷达局部极坐标转换为WGS-84显示坐标。
 * @param device 当前任务绑定的设备台账revision。
 * @param faceIndex 协议携带的物理阵面索引。
 * @param azimuthDeg 阵面局部方位角，单位度；正前方为0且顺时针为正。
 * @param elevationDeg 阵面局部俯仰角，单位度；向上为正。
 * @param slantRangeM 目标斜距，单位米。
 * @return 可绘制的经纬高与NEU分量；找不到阵面姿态时返回null。
 * @note 每条航迹必须使用其faceIndex对应的阵面姿态，不得在多阵面时猜测整机姿态。
 */
export function projectRadarTrack(
  device: RadarDevice,
  faceIndex: number,
  azimuthDeg: number,
  elevationDeg: number,
  slantRangeM: number,
): RadarProjection | null {
  const face = device.faces[faceIndex]
  const absoluteHeading = trackAbsoluteHeading(device, faceIndex)
  if (!face || absoluteHeading === null) return null
  const azimuth = azimuthDeg * DEG_TO_RAD
  const elevation = elevationDeg * DEG_TO_RAD
  const horizontal = slantRangeM * Math.cos(elevation)

  // Local body axes: x=right, y=forward, z=up.
  let x = horizontal * Math.sin(azimuth)
  let y = horizontal * Math.cos(azimuth)
  let z = slantRangeM * Math.sin(elevation)

  // Roll about forward axis (right side down is positive).
  const roll = face.roll * DEG_TO_RAD
  ;[x, z] = [x * Math.cos(roll) + z * Math.sin(roll),
             -x * Math.sin(roll) + z * Math.cos(roll)]

  // Pitch about right axis (nose up is positive).
  const pitch = face.pitch * DEG_TO_RAD
  ;[y, z] = [y * Math.cos(pitch) - z * Math.sin(pitch),
             y * Math.sin(pitch) + z * Math.cos(pitch)]

  // Rotate by the measured absolute heading (clockwise from north).
  const heading = absoluteHeading * DEG_TO_RAD
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

/**
 * @brief 把阵面绝对航向规范到`[0,360)`。
 * @param faceAzimuthDeg 真北顺时针角度，单位度。
 * @return 与输入等价的规范化角度。
 */
export function absoluteFaceHeading(faceAzimuthDeg: number): number {
  return ((faceAzimuthDeg % 360) + 360) % 360
}
