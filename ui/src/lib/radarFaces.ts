/**
 * @file radarFaces.ts
 * @brief 提供设备台账表单共用的阵面编号与安装航向映射。
 * @details 上游是新增/编辑雷达表单，下游是RadarFace草稿；四面阵只由用户指定物理1#绝对航向，
 * 其余阵面按顺时针每90度确定。本模块不修改任务faceMask，也不处理协议控制参数。
 */

import type { RadarFace } from '../components/map/types'

/**
 * @brief 把任意角度规范到真北顺时针的`[0,360)`范围。
 * @param degrees 用户输入或派生出的角度。
 * @return 等价的绝对安装航向。
 */
export function normalizeHeading(degrees: number): number {
  return ((degrees % 360) + 360) % 360
}

/**
 * @brief 建立合同允许的单面阵或四面阵草稿。
 * @param count 阵面数量，只允许1或4。
 * @param firstHeadingDeg 物理1#阵面的实测绝对航向。
 * @param source 可选旧阵面，用于切换形态时保留各面的俯仰、横滚和扫描半角。
 * @return 编号连续且四面航向依次相差90度的新数组。
 */
export function createRadarFaces(
  count: 1 | 4,
  firstHeadingDeg: number,
  source: RadarFace[] = [],
): RadarFace[] {
  const first = normalizeHeading(firstHeadingDeg)
  return Array.from({ length: count }, (_, index) => ({
    label: String.fromCharCode(65 + index),
    heading: normalizeHeading(first + index * 90),
    pitch: source[index]?.pitch ?? source[0]?.pitch ?? 0,
    roll: source[index]?.roll ?? source[0]?.roll ?? 0,
    scanAngle: source[index]?.scanAngle ?? 45,
    active: source[index]?.active ?? true,
  }))
}

/**
 * @brief 更新单个阵面草稿，并在四面阵修改1#航向时同步物理编号映射。
 * @param faces 当前表单阵面数组。
 * @param index 被编辑阵面的数组下标。
 * @param field 被编辑字段。
 * @param value 新值。
 * @return 不修改原数组的新阵面数组。
 */
export function updateRadarFace(
  faces: RadarFace[],
  index: number,
  field: keyof RadarFace,
  value: RadarFace[keyof RadarFace],
): RadarFace[] {
  if (field === 'heading' && index === 0 && faces.length === 4) {
    const first = Number(value)
    return faces.map((face, faceIndex) => ({
      ...face,
      heading: normalizeHeading(first + faceIndex * 90),
    }))
  }
  return faces.map((face, faceIndex) => faceIndex === index
    ? { ...face, [field]: field === 'heading' ? normalizeHeading(Number(value)) : value }
    : face)
}
