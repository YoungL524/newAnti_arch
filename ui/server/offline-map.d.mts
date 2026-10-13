/**
 * @file offline-map.d.mts
 * @brief 声明本地 MBTiles 读取器的 HTTP 生命周期合同；不对前端公开 SQLite 对象。
 */
import type { IncomingMessage, ServerResponse } from 'node:http'

/** @brief 打开服务端指定 MBTiles 及 XYZ 根目录；只读连接由 dispose 释放，缺任一地图源不阻塞其他源与业务 API。 */
export function openOfflineMap(filename: string, xyzDirectory: string): {
  /** @brief 处理地图元数据或 XYZ 请求；不匹配时返回 false 交给后续路由。 */
  handleRequest(request: IncomingMessage, response: ServerResponse): boolean
  /** @brief 幂等释放地图数据库。 */
  dispose(): void
}
