/**
 * @file useTrackData.ts
 * @brief 从 CoreProvider 的最终航迹状态派生数据表行。
 * @details 唯一 WebSocket 及“初始快照 + 增量更新/删除”由 CoreProvider 拥有；本 Hook 只完成当前设备筛选，
 * 不创建连接、不保存后端历史，也不改变断线和版本缺口语义。
 */

import { useMemo } from 'react'
import { DeviceType, type DeviceDataRow, type RadarTrack } from '../lib/protocol'
import { useCore } from './useCoreApi'

/** @brief 表示底部数据表所需的最终派生视图，不作为后端事实或历史缓冲。 */
export interface TrackDataResult {
  rows: DeviceDataRow[] ///< 当前选定设备的显示行。
  deviceType: DeviceType ///< 决定列定义的设备类型。
  isLive: boolean ///< 当前雷达正在采集且至少有一条可见航迹时为 true。
  sourceLabel: string ///< UI 展示使用的数据源名称。
}

/**
 * @brief 从 CoreProvider 当前态派生指定设备的数据表行。
 * @param deviceType 当前数据面板选择的设备类型。
 * @param sourceId 后端稳定设备标识。
 * @param sourceLabel 用户可见设备名称。
 * @return 不拥有 WebSocket 历史、只供本轮 React 渲染使用的最终表格状态。
 * @note 历史未归档的信噪比、外推、信号及景幅省略；消亡不解释为外推，扫描号不解释为景幅。
 */
export function useTrackData(deviceType: DeviceType, sourceId: string, sourceLabel: string): TrackDataResult {
  const { tracks, historyTracks, isHistoryView, acquisition } = useCore()
  const rows = useMemo<DeviceDataRow[]>(() => {
    if (deviceType !== DeviceType.RADAR) {
      return []
    }
    if (isHistoryView) {
      const historicalRows: RadarTrack[] = historyTracks
        .filter(point => point.deviceId === sourceId)
        .map(point => ({
          batchId: point.targetId,
          deviceId: point.deviceId,
          trackId: point.targetId,
          acquisitionId: point.acquisitionId,
          faceIndex: point.faceIndex,
          slantRange: point.range,
          azimuth: point.azimuth * 180 / Math.PI,
          elevation: point.elevation * 180 / Math.PI,
          altitude: point.altitude,
          radialSpeed: point.radialSpeed,
          amplitude: point.amplitude,
          targetType: 0,
          timestamp: point.tsMs,
        }))
      return historicalRows
    }
    return tracks.filter(track => track.deviceId === sourceId)
  }, [tracks, historyTracks, isHistoryView, deviceType, sourceId])

  const isLive = !isHistoryView && deviceType === DeviceType.RADAR && acquisition && rows.length > 0

  return { rows, deviceType, isLive, sourceLabel }
}
