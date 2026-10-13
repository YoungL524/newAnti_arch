/**
 * @file server.d.mts
 * @brief 为开发及生产 Web 入口声明共享终端生命周期接口；运行实现位于 server.mjs。
 */
import type { IncomingMessage, Server, ServerResponse } from 'node:http'

/** @brief 挂接现有 HTTP 服务器；返回令牌路由与清理入口，会话不进入 Qt 数据域。 */
export function installTerminal(server: Server, options?: { workingDirectory?: string }): {
  /** @brief 处理令牌接口并返回是否接管；不匹配时调用者继续自己的路由。 */
  handleRequest(request: IncomingMessage, response: ServerResponse): boolean
  /** @brief 释放连接、PTY 和升级监听，可重复调用。 */
  dispose(): void
}
