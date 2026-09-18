/**
 * @file vite.config.ts
 * @brief 配置前端开发服务器、React 编译插件和生产构建参数。
 * @details 开发环境中的 API 与 WebSocket 代理必须指向核心进程端口，使浏览器代码始终使用同源相对地址。
 */

import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
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
