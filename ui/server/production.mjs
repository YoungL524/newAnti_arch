/**
 * @file production.mjs
 * @brief 提供单机部署的静态网页、只读离线地图、Core HTTP/WebSocket 转发与现有终端入口。
 * @details 启动器提供端口、部署目录和健康令牌；浏览器只访问一个回环入口。
 * 本模块不解析雷达协议、不拥有业务状态、不执行设备 Command；入口为本文件启动代码。
 */
import { createServer, request as coreRequest } from 'node:http'
import { connect as connectCore } from 'node:net'
import { createReadStream, existsSync } from 'node:fs'
import { realpath, stat } from 'node:fs/promises'
import { dirname, extname, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { pipeline } from 'node:stream'
import { installTerminal } from '../terminal/server.mjs'
import { openOfflineMap } from './offline-map.mjs'

const webRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../dist')
const port = Number(process.env.NEWANTI_WEB_PORT || 8080)
const deploymentToken = process.env.NEWANTI_DEPLOYMENT_TOKEN
if (!Number.isInteger(port) || port < 1024 || port > 65535 || !deploymentToken
    || !existsSync(resolve(webRoot, 'index.html'))) {
  throw new Error('部署端口、就绪令牌或已构建的网页缺失')
}
const root = await realpath(webRoot)
const bridges = new Set()
/** @brief 本地地图连接仅属于 Web 服务，不接入 Core 业务数据库。 */
const offlineMap = openOfflineMap(resolve(dirname(fileURLToPath(import.meta.url)), '../maps/basemap.mbtiles'),
  resolve(dirname(fileURLToPath(import.meta.url)), '../maps/xyz'))
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon',
  /** @brief 本地字体沿用标准 MIME，配合 nosniff 在离线生产入口提供字形。 */
  '.woff': 'font/woff', '.woff2': 'font/woff2', '.ttf': 'font/ttf' }

/** @brief 拒绝跨站、非回环和错误 Host；部署第一阶段没有远端访问入口。 */
function local(request, websocket = false) {
  if (!['127.0.0.1', '::ffff:127.0.0.1'].includes(request.socket.remoteAddress)) return false
  const origin = `http://${request.headers.host}`
  try {
    const host = new URL(origin)
    if (!['127.0.0.1', 'localhost'].includes(host.hostname) || Number(host.port) !== port) return false
    const site = request.headers['sec-fetch-site']
    if (site && site !== 'same-origin' && site !== 'none') return false
    return request.headers.origin ? request.headers.origin === origin : !websocket
  } catch { return false }
}

/** @brief 失败时结束请求；已开始流式输出的连接直接销毁，不混入 HTML 或 JSON 错误正文。 */
function failure(response, code) {
  if (response.destroyed) return
  if (response.headersSent) { response.destroy(); return }
  response.writeHead(code, { 'Content-Type': 'text/plain; charset=utf-8' })
  response.end(code === 502 ? '核心服务不可用' : '请求不可用')
}

/** @brief 流式转发 Core 请求，沿用既有路径与结果；断开浏览器同时取消上游，不积压完整业务包。 */
function proxyApi(request, response) {
  const headers = { ...request.headers, host: '127.0.0.1:8088', connection: 'close' }
  delete headers['proxy-authorization']
  const upstream = coreRequest({ hostname: '127.0.0.1', port: 8088, method: request.method,
    path: request.url, headers }, incoming => {
    response.writeHead(incoming.statusCode || 502, incoming.headers)
    pipeline(incoming, response, error => { if (error) upstream.destroy() })
  })
  upstream.setTimeout(30000, () => upstream.destroy(new Error('Core 请求超时')))
  upstream.on('error', () => failure(response, 502))
  response.on('close', () => upstream.destroy())
  pipeline(request, upstream, error => { if (error) upstream.destroy() })
}

/** @brief 限制静态文件到 dist 的真实路径；缺失文件返回 404，避免把缺失脚本伪装成首页。 */
async function staticFile(request, response, pathname) {
  try {
    if (!['GET', 'HEAD'].includes(request.method)) { failure(response, 405); return }
    const path = decodeURIComponent(pathname)
    if (path.includes('\0') || path.includes('\\')) { failure(response, 400); return }
    const candidate = resolve(root, `.${path === '/' ? '/index.html' : path}`)
    const filename = await realpath(candidate)
    if (!filename.startsWith(root + sep) || !(await stat(filename)).isFile()) {
      failure(response, 404); return
    }
    const metadata = await stat(filename)
    response.writeHead(200, { 'Content-Type': types[extname(filename)] || 'application/octet-stream',
      'Content-Length': metadata.size, 'Cache-Control': extname(filename) === '.html' ? 'no-store' : 'no-cache',
      'X-Content-Type-Options': 'nosniff' })
    if (request.method === 'HEAD') { response.end(); return }
    pipeline(createReadStream(filename), response, error => { if (error) response.destroy() })
  } catch { failure(response, 404) }
}

/** @brief 路由仅连接现有生产边界；健康令牌用于确认本次启动实例，不向普通页面公开。 */
const http = createServer((request, response) => {
  if (!local(request)) { failure(response, 403); return }
  const pathname = request.url?.split('?')[0] || '/'
  if (terminal.handleRequest(request, response)) return
  if (offlineMap.handleRequest(request, response)) return
  if (pathname === '/deployment/health') {
    if (request.method !== 'GET' || request.headers['x-deployment-token'] !== deploymentToken) {
      failure(response, 403); return
    }
    response.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' })
    response.end(JSON.stringify({ token: deploymentToken }))
  } else if (pathname === '/api/v1' || pathname.startsWith('/api/v1/')) {
    proxyApi(request, response)
  } else {
    void staticFile(request, response, pathname)
  }
})
const terminal = installTerminal(http, { workingDirectory: process.env.NEWANTI_DEPLOYMENT_ROOT })

/** @brief Core WebSocket 按 TCP 背压双向转发，终端升级交给既有 owner，其他路径拒绝。 */
http.on('upgrade', (request, socket, head) => {
  const pathname = request.url?.split('?')[0]
  if (pathname === '/terminal/ws') return
  if (pathname !== '/ws/v1' || !local(request, true)) { socket.destroy(); return }
  const upstream = connectCore(8089, '127.0.0.1')
  bridges.add(upstream)
  const timeout = setTimeout(() => { upstream.destroy(); socket.destroy() }, 5000)
  /** @brief 任一端失败同时关闭两端；关闭 Web 服务时不会留下转发连接。 */
  const cleanup = () => {
    clearTimeout(timeout)
    bridges.delete(upstream)
    upstream.destroy()
    socket.destroy()
  }
  upstream.on('error', cleanup)
  socket.on('error', cleanup)
  upstream.on('close', cleanup)
  socket.on('close', cleanup)
  upstream.once('data', () => clearTimeout(timeout))
  upstream.once('connect', () => {
    const headers = Object.entries(request.headers).filter(([key]) => key !== 'host')
      .flatMap(([key, value]) => Array.isArray(value) ? value.map(item => `${key}: ${item}`) : [`${key}: ${value}`])
    upstream.write(`GET /ws/v1 HTTP/1.1\r\nHost: 127.0.0.1:8089\r\n${headers.join('\r\n')}\r\n\r\n`)
    if (head.length) upstream.write(head)
    socket.pipe(upstream).pipe(socket)
  })
})

/** @brief 正常关闭时停止终端和转发；控制台隐藏不改变启动失败的非零退出码。 */
http.on('close', () => {
  terminal.dispose()
  offlineMap.dispose()
  for (const bridge of bridges) bridge.destroy()
})
http.on('error', error => { console.error(error.message); process.exitCode = 1; terminal.dispose(); offlineMap.dispose() })
http.listen(port, '127.0.0.1')
