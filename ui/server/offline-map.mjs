/**
 * @file offline-map.mjs
 * @brief 从本地 MBTiles 或固定 z/x/y 目录提供栅格底图，不管理下载与转换。
 * @details 开发与生产 HTTP 入口共用此读取器；只读 SQLite，按请求短暂读取一张瓦片。
 * 不接收雷达数据、不改地图文件、不创建缓存或额外服务；入口为 openOfflineMap。
 */
import { DatabaseSync } from 'node:sqlite'
import { accessSync, constants, createReadStream, statSync } from 'node:fs'
import { realpath, stat } from 'node:fs/promises'
import { resolve, sep } from 'node:path'
import { pipeline } from 'node:stream'

/**
 * @brief 打开固定地图文件；缺失或无效时仅让地图路由返回 503，其余业务服务继续运行。
 * @param filename 服务端决定的本地路径，不接受浏览器指定文件。
 * @param xyzDirectory 服务端指定的 z/x/y 根目录，按请求读单张图片，不扫描目录或建立瓦片缓存。
 * @return 同线程路由和幂等关闭入口；连接由调用方随 HTTP 服务关闭。
 */
export function openOfflineMap(filename, xyzDirectory) {
  let database
  let tileQuery
  let metadata
  let unavailable = '离线地图不可用'
  try {
    /** @brief 先验证固定路径为可读文件，使改名或缺文件能明确定位，而不是只返回 SQLite 的笼统错误。 */
    accessSync(filename, constants.R_OK)
    if (!statSync(filename).isFile()) throw new Error('地图路径不是文件')
    database = new DatabaseSync(filename, { readOnly: true })
    const fields = Object.fromEntries(database.prepare('SELECT name, value FROM metadata').all()
      .map(row => [row.name, row.value]))
    const bounds = String(fields.bounds || '').split(',').map(Number)
    const minzoom = Number(fields.minzoom)
    const maxzoom = Number(fields.maxzoom)
    const mime = { png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', webp: 'image/webp' }[fields.format]
    if (!mime || !Number.isInteger(minzoom) || !Number.isInteger(maxzoom)
        || minzoom < 0 || maxzoom > 22 || minzoom > maxzoom || bounds.length !== 4
        || !bounds.every(Number.isFinite) || bounds[0] >= bounds[2] || bounds[1] >= bounds[3]
        || bounds[0] < -180 || bounds[2] > 180 || bounds[1] < -85.051129 || bounds[3] > 85.051129) {
      throw new Error('需要有效范围和层级的 PNG/JPEG/WebP 栅格 MBTiles')
    }
    tileQuery = database.prepare('SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=? AND length(tile_data)<=4194304')
    metadata = { name: String(fields.name || '本地地图'), bounds, minzoom, maxzoom, mime }
  } catch (error) {
    const reason = error.code === 'ENOENT' ? '文件不存在' : error.code === 'EACCES' || error.code === 'EPERM' ? '没有读取权限' : error.message
    unavailable = `离线地图未加载：${reason}；读取路径：${filename}`
    console.error(unavailable)
    database?.close()
    database = undefined
  }

  /** @brief 在固定目录内读取单张 XYZ 图片；符号链接不得越过真实根目录，缺图不回退联网。 */
  async function sendXyz(request, response, zoom, x, y) {
    try {
      const root = await realpath(xyzDirectory)
      for (const [extension, mime] of Object.entries({ png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', webp: 'image/webp' })) {
        let filename
        try { filename = await realpath(resolve(root, String(zoom), String(x), `${y}.${extension}`)) }
        catch (error) { if (error.code === 'ENOENT' || error.code === 'ENOTDIR') continue; throw error }
        if (!filename.startsWith(root + sep)) throw new Error('瓦片路径超出根目录')
        const file = await stat(filename)
        if (!file.isFile() || file.size > 4194304) throw new Error('瓦片必须是 4 MiB 以内的图片文件')
        if (response.destroyed) return
        response.writeHead(200, { 'Content-Type': mime, 'Content-Length': file.size,
          'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' })
        if (request.method === 'HEAD') { response.end(); return }
        pipeline(createReadStream(filename), response, error => { if (error) response.destroy() })
        return
      }
      if (!response.destroyed) { response.writeHead(404); response.end() }
    } catch {
      if (!response.destroyed) {
        if (response.headersSent) { response.destroy(); return }
        response.writeHead(503, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' })
        response.end(request.method === 'HEAD' ? undefined : '本地 z/x/y 地图不可用')
      }
    }
  }

  /** @brief 仅处理 /map 路径；仅 MBTiles 转 TMS 行号，目录瓦片保持 XYZ 行号；两种源均不回退联网。 */
  function handleRequest(request, response) {
    const pathname = request.url?.split('?')[0] || '/'
    if (pathname !== '/map/metadata' && !pathname.startsWith('/map/tiles/') && !pathname.startsWith('/map/xyz/')) return false
    /** @brief 地图响应均禁止嗅探与旧包缓存；HEAD 只返回头部，不发送正文。 */
    const reply = (status, body, mime = 'text/plain; charset=utf-8') => {
      response.writeHead(status, { 'Content-Type': mime, 'Content-Length': Buffer.byteLength(body),
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' })
      response.end(request.method === 'HEAD' ? undefined : body)
    }
    if (!['GET', 'HEAD'].includes(request.method)) { reply(405, '只支持 GET/HEAD'); return true }
    if (pathname.startsWith('/map/xyz/')) {
      const match = /^\/map\/xyz\/(\d{1,2})\/(\d{1,10})\/(\d{1,10})$/.exec(pathname)
      if (!match) { reply(400, '瓦片坐标无效'); return true }
      const [zoom, x, y] = match.slice(1).map(Number)
      if (zoom > 22 || x >= 2 ** zoom || y >= 2 ** zoom) { reply(400, '瓦片坐标无效'); return true }
      void sendXyz(request, response, zoom, x, y)
      return true
    }
    if (!database || !metadata) { reply(503, unavailable); return true }
    if (pathname === '/map/metadata') {
      reply(200, JSON.stringify(metadata), 'application/json; charset=utf-8')
      return true
    }
    const match = /^\/map\/tiles\/(\d{1,2})\/(\d{1,10})\/(\d{1,10})$/.exec(pathname)
    if (!match) { reply(400, '瓦片坐标无效'); return true }
    const [zoom, x, y] = match.slice(1).map(Number)
    if (zoom < metadata.minzoom || zoom > metadata.maxzoom || x >= 2 ** zoom || y >= 2 ** zoom) {
      reply(404, '地图包无此瓦片'); return true
    }
    try {
      const row = tileQuery.get(zoom, x, 2 ** zoom - 1 - y)
      if (!row) reply(404, '地图包无此瓦片')
      else reply(200, row.tile_data, metadata.mime)
    } catch (error) {
      console.error('离线瓦片读取失败：', error.message)
      reply(503, '离线瓦片读取失败')
    }
    return true
  }

  return {
    handleRequest,
    /** @brief 服务关闭时释放唯一只读数据库句柄，不修改地图文件。 */
    dispose() { database?.close(); database = undefined },
  }
}
