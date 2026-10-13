/**
 * @file mapViewport.ts
 * @brief 让既有雷达和航迹 Canvas 共用 AMap 在线与 Leaflet 离线的投影及事件边界。
 * @details 上游是 MapRadarView 拥有的真实地图实例，下游是 PPI/航迹绘制层；不模拟任何 SDK，
 * 不拥有地图、业务数据或完整历史。只统一两种已存在的投影和交互入口；入口为两个具体构造函数。
 */
import type { Map as LeafletMap, LeafletMouseEvent } from 'leaflet'

/** @brief 借用当前地图的绘图接口；所有方法在浏览器主线程调用，生命周期不得超过地图实例。 */
export interface MapViewport {
  /** @brief 返回画布尺寸与点击坐标共同使用的容器，调用方不移除或接管此元素。 */
  getContainer(): HTMLElement
  /** @brief 经纬度单位为度，返回相对容器的 CSS 像素；x 向右、y 向下，不负责业务坐标校准。 */
  project(lng: number, lat: number): { x: number; y: number }
  /** @brief 订阅视图移动、缩放和尺寸变化；返回取消函数，调用方卸载时必须执行。 */
  watchView(listener: () => void): () => void
  /** @brief 接收容器像素点击；handled 用于防止同次点击选中多设备航迹，不持久保存事件。 */
  watchClick(listener: (point: { x: number; y: number; handled: boolean }) => void): () => void
}

/** @brief 将真实 Leaflet 实例接到画布边界，不新增地图或复制瓦片。 */
export function leafletViewport(map: LeafletMap): MapViewport {
  return {
    getContainer: () => map.getContainer(),
    project: (lng, lat) => map.latLngToContainerPoint([lat, lng]),
    /** @brief Leaflet 的移动、缩放与尺寸事件共用重画入口，取消时一次解除三类监听。 */
    watchView(listener) {
      map.on('move zoom resize', listener)
      return () => { map.off('move zoom resize', listener) }
    },
    /** @brief Leaflet 点击使用容器像素，处理标记写回同次事件供其他设备层判断。 */
    watchClick(listener) {
      /** @brief 将同次原生事件上的已处理标记带回 SDK，保持原多雷达点击互斥规则。 */
      const clicked = (event: LeafletMouseEvent & { __trackInteractionHandled?: boolean }) => {
        if (event.__trackInteractionHandled) return
        const point = { x: event.containerPoint.x, y: event.containerPoint.y, handled: false }
        listener(point)
        if (point.handled) event.__trackInteractionHandled = true
      }
      map.on('click', clicked)
      return () => { map.off('click', clicked) }
    },
  }
}

/** @brief 将原 AMap 实例接到相同绘图边界；SDK 的动态类型仅停留在此处，不进入业务数据域。 */
export function amapViewport(map: any, AMap: any): MapViewport {
  return {
    getContainer: () => map.getContainer(),
    project: (lng, lat) => map.lngLatToContainer(new AMap.LngLat(lng, lat)),
    /** @brief AMap 保留原事件语义，只通知画布重画，不向覆盖层传递 SDK 对象。 */
    watchView(listener) {
      for (const event of ['mapmove', 'zoomchange', 'resize']) map.on(event, listener)
      return () => { for (const event of ['mapmove', 'zoomchange', 'resize']) map.off(event, listener) }
    },
    /** @brief AMap 的 pixel 与离线画布使用同一容器坐标合同，复用原点击互斥规则。 */
    watchClick(listener) {
      /** @brief 只归一化当前点击的 CSS 像素，保留 AMap 原始事件的多设备互斥标记。 */
      const clicked = (event: any) => {
        if (event.__trackInteractionHandled || !event.pixel) return
        const point = { x: event.pixel.x, y: event.pixel.y, handled: false }
        listener(point)
        if (point.handled) event.__trackInteractionHandled = true
      }
      map.on('click', clicked)
      return () => { map.off('click', clicked) }
    },
  }
}
