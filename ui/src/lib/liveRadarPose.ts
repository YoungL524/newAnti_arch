/**
 * @file liveRadarPose.ts
 * @brief 将 DP 心跳实时位姿覆盖到雷达显示副本。
 * @details 上游为 DeviceState WebSocket，下游为配置页与地图；不修改设备台账对象，也不发保存请求。
 */

import type { RadarDevice } from '../components/map/types'
import type { RadarLivePose } from './protocol'

/**
 * @brief 仅在稳定设备 ID 匹配时覆盖在线阵面平均位置及各面独立姿态。
 * @param device 设备台账或当前表单草稿，用于离线阵面及无实时位姿时回退。
 * @param pose DP 心跳实时位姿；缺失或属于其他设备时不改动输入。
 * @return 供显示使用的设备副本；不得交给设备台账保存接口。
 */
export function applyLiveRadarPose(device: RadarDevice, pose?: RadarLivePose): RadarDevice {
  if (!pose || pose.deviceId !== device.id) return device
  return {
    ...device,
    lng: pose.longitudeDeg,
    lat: pose.latitudeDeg,
    altitude: pose.altitudeM,
    faces: device.faces.map((face, index) => {
      const liveFace = pose.faces.find(item => item.faceIndex === index)
      return liveFace ? {
        ...face,
        heading: (liveFace.headingDeg + 360) % 360,
        pitch: liveFace.pitchDeg,
        roll: liveFace.rollDeg,
      } : face
    }),
  }
}
