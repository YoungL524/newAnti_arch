/**
 * @file TerminalPanel.tsx
 * @brief 在底部面板呈现本机交互终端；输入直达 PTY，输出直达 xterm.js。
 * @details 本组件唯一拥有终端、连接和尺寸观察器。React 仅保存连接状态与 Shell 选择，
 * 不保存输出字符串或逐字符更新页面。隐藏时保留会话，卸载时断开并释放资源；入口为 TerminalPanel。
 */
import { memo, useEffect, useRef, useState } from 'react'
import { Terminal } from '@xterm/xterm'
import { FitAddon } from '@xterm/addon-fit'
import '@xterm/xterm/css/xterm.css'

/** @brief 固定允许的 Windows Shell；可执行文件和工作目录由服务器确定。 */
type Shell = 'powershell' | 'cmd'

/**
 * @brief 维护一个交互会话，并提供显式连接、断开与复制操作。
 * @return 终端工具栏和 xterm.js 容器；切换面板可见性不会销毁会话。
 * @note Ctrl+C 发给 Shell；Ctrl+Shift+C 复制选区，Ctrl+V 使用浏览器粘贴事件。
 */
function TerminalPanel() {
  const containerRef = useRef<HTMLDivElement>(null)
  const connectRef = useRef<(shell: Shell) => void>(() => {})
  const disconnectRef = useRef<() => void>(() => {})
  const copyRef = useRef<() => void>(() => {})
  const [shell, setShell] = useState<Shell>('powershell')
  const [phase, setPhase] = useState<'connecting' | 'ready' | 'closed'>('connecting')
  const [notice, setNotice] = useState('正在连接…')

  /**
   * @brief 终端只在挂载时创建一次；二进制输出完成解析后按实际字节数确认，形成端到端流控。
   * @details 尺寸变化合并到下一帧，只有实际行列变化才发送 resize；隐藏时不改变终端尺寸。
   * 异步认证以连接代次隔离，防止断开或 StrictMode 卸载后再创建后台会话。
   */
  useEffect(() => {
    const container = containerRef.current
    if (!container) return
    let disposed = false
    let socket: WebSocket | null = null
    let ready = false
    let generation = 0
    let request: AbortController | null = null
    let resizeFrame = 0
    const terminal = new Terminal({
      cursorBlink: true,
      fontFamily: '"Cascadia Mono", Consolas, "Microsoft YaHei", monospace',
      fontSize: 14,
      scrollback: 3000,
      allowProposedApi: false,
      theme: { background: '#111827', foreground: '#e5e7eb', cursor: '#34d399', selectionBackground: '#374151' },
    })
    const fit = new FitAddon()
    terminal.loadAddon(fit)
    terminal.open(container)

    /** @brief 连接仍有效时发送有界控制消息；客户端发送积压超过 256 KiB 时结束会话。 */
    function send(message: object) {
      if (!socket || socket.readyState !== WebSocket.OPEN) return
      if (socket.bufferedAmount > 256 * 1024) {
        setNotice('终端输入积压过多，已断开；请重新连接')
        disconnect()
        return
      }
      socket.send(JSON.stringify(message))
    }

    /** @brief 从可见容器推导字符几何，限制到服务器接受范围，不因折叠发送零尺寸。 */
    function resize() {
      resizeFrame = 0
      if (disposed || container!.clientWidth === 0 || container!.clientHeight === 0) return
      const dimensions = fit.proposeDimensions()
      if (!dimensions) return
      const cols = Math.max(2, Math.min(500, dimensions.cols))
      const rows = Math.max(2, Math.min(200, dimensions.rows))
      if (terminal.cols === cols && terminal.rows === rows) return
      terminal.resize(cols, rows)
      if (ready) send({ type: 'resize', cols, rows })
    }

    /** @brief 同一帧内合并布局变化，避免拖动或面板动画连续重排终端。 */
    function scheduleResize() {
      if (!resizeFrame) resizeFrame = requestAnimationFrame(resize)
    }

    /** @brief 输入按最多 4096 个 UTF-16 单元分块，切分时保留代理对；不把粘贴文本加入 React 状态。 */
    function input(data: string, binary = false) {
      if (!ready) return
      for (let offset = 0; offset < data.length;) {
        let end = Math.min(offset + 4096, data.length)
        if (!binary && end < data.length && /[\uD800-\uDBFF]/.test(data[end - 1])) end--
        send({ type: 'input', data: data.slice(offset, end), binary })
        offset = end
        if (!ready) break
      }
    }

    /** @brief 显式断开也取消尚未完成的认证请求；连接 close 后才允许再次启动 Shell。 */
    function disconnect() {
      generation++
      request?.abort()
      request = null
      ready = false
      terminal.options.disableStdin = true
      if (socket && socket.readyState < WebSocket.CLOSING) {
        setPhase('connecting')
        socket.close(1000, '用户断开')
      } else if (!socket) {
        setPhase('closed')
        setNotice('连接已取消，可重新连接')
      }
    }

    /** @brief 创建经过同源令牌认证的 WebSocket；连接失败保留屏幕内容并允许手动重连。 */
    async function connect(selected: Shell) {
      if (socket || disposed) return
      const attempt = ++generation
      request?.abort()
      request = new AbortController()
      setPhase('connecting')
      setNotice('正在连接…')
      ready = false
      terminal.options.disableStdin = true
      try {
        resize()
        const response = await fetch('/terminal/token', {
          headers: { 'X-Terminal-Client': '1' }, cache: 'no-store', signal: request.signal,
        })
        if (!response.ok) throw new Error('终端服务未就绪，请确认应用已启动，并通过应用的 Web 地址打开页面')
        const auth = await response.json() as { token?: unknown }
        if (typeof auth.token !== 'string') throw new Error('终端服务响应无效')
        if (disposed || attempt !== generation) return
        request = null
        const endpoint = new URL('/terminal/ws', window.location.href)
        endpoint.protocol = endpoint.protocol === 'https:' ? 'wss:' : 'ws:'
        const client = new WebSocket(endpoint)
        socket = client
        client.binaryType = 'arraybuffer'
        let explicitResult = false

        /** @brief 认证消息不放入 URL，避免令牌出现在访问日志；初始几何取当前容器尺寸。 */
        client.onopen = () => {
          if (disposed || attempt !== generation) { client.close(); return }
          send({ type: 'start', token: auth.token, shell: selected, cols: terminal.cols, rows: terminal.rows })
        }

        /** @brief UTF-8 数据跨帧解析由 xterm.js 处理；解析回调才报告已消费字节，后台页签也保持有界。 */
        client.onmessage = event => {
          if (disposed || client !== socket || attempt !== generation) return
          if (event.data instanceof ArrayBuffer) {
            const bytes = new Uint8Array(event.data)
            terminal.write(bytes, () => {
              if (!disposed && client === socket && attempt === generation) {
                send({ type: 'ack', bytes: bytes.byteLength })
              }
            })
            return
          }
          try {
            const message = JSON.parse(event.data as string)
            if (message.type === 'ready') {
              terminal.reset()
              terminal.options.windowsPty = { backend: 'conpty', buildNumber: message.build }
              terminal.options.disableStdin = false
              ready = true
              setPhase('ready')
              setNotice(`${message.shell === 'cmd' ? 'CMD' : 'PowerShell'} · ${message.cwd}`)
              resize()
              terminal.focus()
            } else if (message.type === 'error') {
              explicitResult = true
              setNotice(String(message.message))
            } else if (message.type === 'exit') {
              explicitResult = true
              setNotice(`Shell 已退出（${message.code}），可重新连接`)
            }
          } catch {
            explicitResult = true
            setNotice('终端收到无效响应，已断开')
            disconnect()
          }
        }

        /** @brief 关闭后释放连接引用，不自动重连或重放输入，避免重复执行命令。 */
        client.onclose = () => {
          if (disposed || client !== socket) return
          socket = null
          ready = false
          terminal.options.disableStdin = true
          setPhase('closed')
          if (!explicitResult) setNotice('终端已断开，可重新连接；同一 Web 服务只允许一个会话')
        }
        /** @brief 浏览器隐藏握手失败详情时提供入口提示；具体生命周期仍以 close 为准。 */
        client.onerror = () => {
          if (disposed || client !== socket) return
          explicitResult = true
          setNotice('连接失败：请确认通过本机 Web 服务打开页面，且没有其他终端会话')
        }
      } catch (error) {
        if (disposed || attempt !== generation) return
        request = null
        setPhase('closed')
        setNotice(error instanceof Error ? error.message : '连接失败')
      }
    }

    /** @brief 只复制 xterm.js 选区；剪贴板权限失败显示提示，不改变 Shell 输入。 */
    async function copy() {
      const selected = terminal.getSelection()
      if (!selected) return
      try { await navigator.clipboard.writeText(selected) }
      catch { if (!disposed) setNotice('浏览器不允许写入剪贴板，请使用浏览器复制菜单') }
    }

    /** @brief 复制快捷键交给浏览器剪贴板，其余按键保留 xterm.js 的终端语义。 */
    terminal.attachCustomKeyEventHandler(event => {
      if (event.type === 'keydown' && event.ctrlKey && event.shiftKey && event.code === 'KeyC') {
        event.preventDefault()
        void copy()
        return false
      }
      return true
    })
    const dataSubscription = terminal.onData(data => input(data))
    const binarySubscription = terminal.onBinary(data => input(data, true))
    const observer = new ResizeObserver(scheduleResize)
    observer.observe(container)
    connectRef.current = selected => { void connect(selected) }
    disconnectRef.current = disconnect
    copyRef.current = () => { void copy() }
    void connect('powershell')

    /** @brief 卸载时取消请求、停止布局调度并关闭连接；服务器据断开事件终止该 PTY。 */
    return () => {
      disposed = true
      generation++
      request?.abort()
      socket?.close(1000, '页面关闭')
      observer.disconnect()
      cancelAnimationFrame(resizeFrame)
      dataSubscription.dispose()
      binarySubscription.dispose()
      terminal.dispose()
      connectRef.current = () => {}
      disconnectRef.current = () => {}
      copyRef.current = () => {}
    }
  }, [])

  return (
    <div className="flex flex-col flex-1 min-h-0" style={{ background: '#111827', color: '#e5e7eb' }}>
      <div className="flex items-center gap-2 px-3 py-1 shrink-0 text-[11px]">
        <select aria-label="终端 Shell" value={shell} disabled={phase !== 'closed'}
          onChange={event => setShell(event.target.value as Shell)}
          className="rounded px-1 py-0.5" style={{ background: '#1f2937', color: '#e5e7eb' }}>
          <option value="powershell">PowerShell</option><option value="cmd">CMD</option>
        </select>
        <button
          className="px-2 py-0.5 rounded border-none cursor-pointer disabled:opacity-40"
          style={{ background: '#374151', color: '#e5e7eb' }}
          onClick={() => phase !== 'closed' ? disconnectRef.current() : connectRef.current(shell)}>
          {phase === 'ready' ? '断开' : phase === 'connecting' ? '取消' : '连接'}
        </button>
        <button className="px-2 py-0.5 rounded border-none cursor-pointer"
          style={{ background: '#374151', color: '#e5e7eb' }} onClick={() => copyRef.current()}>复制选区</button>
        <span role="status" className="truncate flex-1" title={notice}>{notice}</span>
        <span className="hidden xl:inline text-[10px]" style={{ color: '#9ca3af' }}>Ctrl+C 中断 · Ctrl+Shift+C 复制 · Ctrl+V 粘贴</span>
      </div>
      <div ref={containerRef} className="flex-1 min-h-0 px-2 pb-1" aria-label="本机交互终端" />
    </div>
  )
}

/** @brief 屏蔽父面板日志更新引起的无关渲染，连接状态仍由组件自身正常更新。 */
export default memo(TerminalPanel)
