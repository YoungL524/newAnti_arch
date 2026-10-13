/**
 * @file server.mjs
 * @brief 将本机 Windows 伪终端挂接到开发或生产 Web 进程。
 * @details 浏览器输入经认证 WebSocket 进入 node-pty，UTF-8 输出直接返回 xterm.js；
 * 不读取数据域、不写日志数据库、不经 Qt 控制平面执行设备命令。入口为 installTerminal()。
 * 会话由当前连接唯一拥有，断开销毁进程树；只允许回环客户端和同源浏览器，最多一个会话。
 */
import { randomBytes, timingSafeEqual } from 'node:crypto'
import { existsSync } from 'node:fs'
import { release } from 'node:os'
import { join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocket, WebSocketServer } from 'ws'

const repositoryRoot = resolve(fileURLToPath(new URL('../../', import.meta.url)))
const highWater = 256 * 1024
const lowWater = 64 * 1024
const hardLimit = 1024 * 1024

/**
 * @brief 校验连接来自回环地址、Host 使用当前监听端口且 Origin 与实际 Web 入口一致。
 * @param request Node HTTP 请求；不信任代理提供的 X-Forwarded-* 身份。
 * @param requireOrigin WebSocket 必须提供 Origin，普通同源 fetch 可以没有 Origin。
 * @return 任一身份或浏览器跨站标记不符合时返回 false。
 */
function trusted(request, requireOrigin = false) {
  const remote = request.socket.remoteAddress
  if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(remote)) return false
  const protocol = request.socket.encrypted ? 'https:' : 'http:'
  try {
    const origin = `${protocol}//${request.headers.host}`
    const host = new URL(origin)
    if (!['localhost', '127.0.0.1', '[::1]'].includes(host.hostname)) return false
    if (Number(host.port || (protocol === 'https:' ? 443 : 80)) !== request.socket.localPort) return false
    const site = request.headers['sec-fetch-site']
    if (site && site !== 'same-origin' && site !== 'none') return false
    return request.headers.origin ? request.headers.origin === origin : !requireOrigin
  } catch {
    return false
  }
}

/** @brief 限制终端几何尺寸，防止输入异常导致原生伪终端申请巨大屏幕缓冲区。 */
function validSize(message) {
  return Number.isInteger(message.cols) && message.cols >= 2 && message.cols <= 500
    && Number.isInteger(message.rows) && message.rows >= 2 && message.rows <= 200
}

/**
 * @brief 给已有 HTTP 服务器挂接终端升级监听，并返回路由与清理入口。
 * @param http 调用者拥有的 HTTP 服务器；本函数不创建额外监听端口。
 * @param options workingDirectory 指定 Shell 初始目录；省略时使用开发仓库目录。
 * @return handleRequest 只处理令牌接口，返回是否已接管请求；dispose 幂等清理会话。
 * @note Node 事件循环独占所有会话状态；node-pty 仅在通过认证并请求连接时加载。
 */
export function installTerminal(http, options = {}) {
  const workingDirectory = options.workingDirectory ?? repositoryRoot
  const token = randomBytes(32).toString('hex')
  const sockets = new WebSocketServer({ noServer: true, maxPayload: 64 * 1024, perMessageDeflate: false })
  let active = null
  let cleanupActive = null
  let stopped = false

  /** @brief 仅向同源本机页面交付启动期令牌，不允许缓存或跨域读取。 */
  function handleRequest(request, response) {
    if (request.url?.split('?')[0] !== '/terminal/token') return false
    response.setHeader('Cache-Control', 'no-store')
    if (request.method !== 'GET' || !trusted(request) || request.headers['x-terminal-client'] !== '1') {
      response.writeHead(403).end()
      return true
    }
    response.setHeader('Content-Type', 'application/json; charset=utf-8')
    response.end(JSON.stringify({ token }))
    return true
  }

  /** @brief 只接管终端路径，保留雷达 WebSocket 的既有代理路由。 */
  const upgrade = (request, socket, head) => {
    if (request.url?.split('?')[0] !== '/terminal/ws') return
    if (!trusted(request, true) || active) {
      socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n')
      return
    }
    sockets.handleUpgrade(request, socket, head, client => {
      active = client
      attachSession(client)
    })
  }

  /**
   * @brief 一个连接拥有一个 PTY；输入只发送原始按键，不包装成设备 Command。
   * @details 超过 256 KiB 未解析输出时暂停 PTY，低于 64 KiB 恢复；超过 1 MiB 关闭会话。
   * ACK 仅证明浏览器解析了输出，不代表任何命令执行成功。退出前排空已产生输出。
   */
  function attachSession(client) {
    let terminal = null
    let starting = false
    let ended = false
    let exitCode = null
    let pending = []
    let pendingBytes = 0
    let inFlight = 0
    let paused = false
    let flushTask = null
    let exitTimer = null
    const subscriptions = []
    const authTimer = setTimeout(() => fail('连接认证超时'), 5000)

    /** @brief 发送少量状态信息；输出正文通过二进制帧传输，避免 JSON 转义开销。 */
    function status(message) {
      if (client.readyState === WebSocket.OPEN) client.send(JSON.stringify(message))
    }

    /** @brief 失败只结束当前会话，保留 Vite 页面和 Qt 雷达进程。 */
    function fail(message) {
      status({ type: 'error', message })
      client.close(1011, '终端会话结束')
      cleanup()
    }

    /** @brief 清理延迟任务、订阅和进程树；连接关闭与服务器关闭可重复触发。 */
    function cleanup() {
      if (ended) return
      ended = true
      clearTimeout(authTimer)
      clearTimeout(exitTimer)
      if (flushTask !== null) clearImmediate(flushTask)
      for (const subscription of subscriptions) subscription.dispose()
      pending = []
      if (terminal && exitCode === null) {
        try { terminal.kill() } catch { /* 原生进程可能已在断开前退出。 */ }
      }
      terminal = null
      if (active === client) {
        active = null
        cleanupActive = null
      }
    }

    /** @brief 对未解析字节应用迟滞流控，暂停的是读取端，不丢弃终端字符。 */
    function flow() {
      if (!terminal || exitCode !== null) return
      const bytes = pendingBytes + inFlight
      if (!paused && bytes >= highWater) {
        terminal.pause()
        paused = true
      } else if (paused && bytes <= lowWater) {
        terminal.resume()
        paused = false
      }
    }

    /** @brief Shell 退出后等待尾部数据被浏览器解析，再报告退出码和关闭连接。 */
    function finishExit() {
      if (exitCode === null || pendingBytes || inFlight || ended) return
      status({ type: 'exit', code: exitCode })
      client.close(1000, 'Shell 已退出')
      cleanup()
    }

    /** @brief 同一轮 PTY 输出合并一次，按 32 KiB 帧发送；不按字符创建 UI 消息。 */
    function flush() {
      flushTask = null
      if (ended || client.readyState !== WebSocket.OPEN) return
      const bytes = Buffer.concat(pending, pendingBytes)
      pending = []
      pendingBytes = 0
      inFlight += bytes.length
      for (let offset = 0; offset < bytes.length; offset += 32 * 1024) {
        client.send(bytes.subarray(offset, offset + 32 * 1024), { binary: true }, error => {
          if (error && !ended) fail('终端输出发送失败')
        })
      }
      flow()
      finishExit()
    }

    /** @brief 按连接期令牌认证后启动固定 Shell；工作目录和可执行文件不接受浏览器任意路径。 */
    async function start(message) {
      const provided = Buffer.from(typeof message.token === 'string' ? message.token : '')
      const expected = Buffer.from(token)
      if (provided.length !== expected.length || !timingSafeEqual(provided, expected)
          || !validSize(message) || !['powershell', 'cmd'].includes(message.shell)) {
        fail('终端认证或启动参数无效')
        return
      }
      starting = true
      clearTimeout(authTimer)
      try {
        if (process.platform !== 'win32' || Number(release().split('.')[2]) < 18309) {
          throw new Error('本功能需要支持 ConPTY 的 Windows 10/11')
        }
        const pty = await import('node-pty')
        if (ended) return
        const systemRoot = process.env.SystemRoot || process.env.SYSTEMROOT
        if (!systemRoot) throw new Error('系统缺少 SystemRoot 环境变量')
        const executable = message.shell === 'cmd'
          ? join(systemRoot, 'System32', 'cmd.exe')
          : join(systemRoot, 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe')
        if (!existsSync(executable)) throw new Error('未找到所选 Shell')
        terminal = pty.spawn(executable, message.shell === 'cmd' ? ['/Q'] : ['-NoLogo', '-NoProfile'], {
          name: 'xterm-256color', cols: message.cols, rows: message.rows,
          cwd: workingDirectory, env: { ...process.env, TERM: 'xterm-256color' }, useConpty: true,
        })
        /** @brief PTY 输出只短暂驻留到下一次 flush；积压硬上限防止浏览器停顿消耗无限内存。 */
        subscriptions.push(terminal.onData(data => {
          if (ended) return
          const bytes = Buffer.from(data, 'utf8')
          if (pendingBytes + inFlight + bytes.length > hardLimit) {
            fail('浏览器输出积压超过 1 MiB，已结束终端会话')
            return
          }
          pending.push(bytes)
          pendingBytes += bytes.length
          flow()
          if (flushTask === null) flushTask = setImmediate(flush)
        }))
        /** @brief 保留退出前的所有输出，最多等待五秒供浏览器排空，随后释放连接。 */
        subscriptions.push(terminal.onExit(event => {
          exitCode = event.exitCode
          exitTimer = setTimeout(() => { client.close(1000); cleanup() }, 5000)
          finishExit()
        }))
        status({ type: 'ready', shell: message.shell, cwd: workingDirectory, build: Number(release().split('.')[2]) })
      } catch (error) {
        fail(`终端启动失败：${error instanceof Error ? error.message : String(error)}`)
      }
    }

    /** @brief 验证消息类型和边界；只在已创建且未退出的 PTY 上执行输入与缩放。 */
    client.on('message', (data, binary) => {
      if (ended) return
      try {
        if (binary) throw new Error('客户端消息必须为 JSON')
        const message = JSON.parse(data.toString('utf8'))
        if (!message || typeof message !== 'object') throw new Error('无效消息')
        if (!terminal && !starting && message.type === 'start') { void start(message); return }
        if (!terminal) throw new Error('终端尚未就绪')
        if (message.type === 'ack') {
          if (!Number.isInteger(message.bytes) || message.bytes <= 0 || message.bytes > inFlight) {
            throw new Error('输出确认字节数无效')
          }
          inFlight -= message.bytes
          flow()
          finishExit()
        } else if (exitCode !== null) {
          return
        } else if (message.type === 'input' && typeof message.data === 'string'
                   && Buffer.byteLength(message.data, 'utf8') <= 16 * 1024) {
          if (message.binary === true) {
            if ([...message.data].some(character => character.charCodeAt(0) > 255)) throw new Error('二进制按键无效')
            terminal.write(Buffer.from(message.data, 'latin1'))
          } else {
            terminal.write(message.data)
          }
        } else if (message.type === 'resize' && validSize(message)) {
          terminal.resize(message.cols, message.rows)
        } else {
          throw new Error('终端消息或尺寸无效')
        }
      } catch (error) {
        fail(error instanceof Error ? error.message : '终端消息处理失败')
      }
    })
    client.on('close', cleanup)
    client.on('error', cleanup)
    cleanupActive = cleanup
  }

  /** @brief Web 服务或 Node 进程停止时终止会话并移除升级监听；不等待浏览器继续发消息。 */
  function dispose() {
    if (stopped) return
    stopped = true
    http.off('upgrade', upgrade)
    http.off('close', dispose)
    process.off('exit', dispose)
    cleanupActive?.()
    for (const client of sockets.clients) client.terminate()
    sockets.close()
  }
  http.on('upgrade', upgrade)
  http.once('close', dispose)
  process.once('exit', dispose)
  return { handleRequest, dispose }
}
