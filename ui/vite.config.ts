/**
 * @file vite.config.ts
 * @brief 配置前端开发服务器、React 编译插件和生产构建参数。
 * @details 开发环境中的 API 与 WebSocket 代理必须指向核心进程端口，使浏览器代码始终使用同源相对地址。
 */

import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { installTerminal } from './terminal/server.mjs'
import { openOfflineMap } from './server/offline-map.mjs'
import { fileURLToPath } from 'node:url'
import type { PreviewServer, ViteDevServer } from 'vite'

/** @brief 保存当前 Web 服务器终端清理入口，Vite 插件关闭时不留存 PTY 会话。 */
let stopTerminal: (() => void) | undefined
/** @brief 地图连接随开发/预览 HTTP 服务释放，不进入构建产物中的浏览器脚本。 */
let stopOfflineMap: (() => void) | undefined

/** @brief 开发与预览复用地图和终端路由；终端依赖 HTTP/1 升级，显式排除 HTTP/2 服务器。 */
function attachTerminal(server: ViteDevServer | PreviewServer) {
  if (!server.httpServer || !('maxHeadersCount' in server.httpServer)) return
  const terminal = installTerminal(server.httpServer)
  const offlineMap = openOfflineMap(fileURLToPath(new URL('./maps/basemap.mbtiles', import.meta.url)),
    fileURLToPath(new URL('./maps/xyz', import.meta.url)))
  stopOfflineMap = offlineMap.dispose
  server.httpServer.once('close', offlineMap.dispose)
  stopTerminal = terminal.dispose
  server.middlewares.use((request, response, next) => {
    if (!terminal.handleRequest(request, response)) next()
  })
  server.middlewares.use((request, response, next) => {
    if (!offlineMap.handleRequest(request, response)) next()
  })
}

export default defineConfig({
  plugins: [react(), {
    name: 'local-terminal',
    /** @brief 开发服务器就绪前挂接同源终端，关闭服务器时由服务入口清理 PTY。 */
    configureServer(server) { attachTerminal(server) },
    /** @brief 本机生产预览复用相同终端入口，不另开 Shell 监听端口。 */
    configurePreviewServer(server) { attachTerminal(server) },
    /** @brief Vite dev 关闭插件容器时主动清理升级连接，避免依赖 HTTP close 的完成顺序。 */
    closeBundle() { stopTerminal?.(); stopTerminal = undefined; stopOfflineMap?.(); stopOfflineMap = undefined },
  }],
  server: {
    port: 5173,
    strictPort: false,
    proxy: {
      '/api': {
        target: 'http://localhost:8088',
        configure: (proxy) => {
          proxy.on('error', (err: Error & { code?: string; errors?: Array<{ code?: string }> }) => {
            const codes: string[] = err.code
              ? [err.code]
              : (err.errors?.map((e) => e.code).filter((code): code is string => Boolean(code)) ?? [])
            const silent = new Set(['ECONNREFUSED', 'ECONNRESET', 'EPIPE', 'ECONNABORTED'])
            const msg = err.message || ''
            if (codes.some((c) => silent.has(c))) return
            if (msg.includes('socket hang up')) return
            console.error('[vite] api proxy error:', err.message)
          })
        },
      },
      '/ws': {
        target: 'ws://localhost:8089',
        ws: true,
        rewrite: (path) => path.replace(/^\/ws(?:\/v1)?/, '/ws/v1'),
      },
    },
  },
  build: {
    outDir: 'dist',
  },
})
