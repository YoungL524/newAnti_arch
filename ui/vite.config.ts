import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
    server: {
    port: 5173,
    strictPort: true,
    proxy: {
      '/api': {
        target: 'http://localhost:8088',
        configure: (proxy) => {
          proxy.on('error', (err: NodeJS.ErrnoException & { errors?: NodeJS.ErrnoException[] }) => {
            const codes = err.code
              ? [err.code]
              : (err.errors?.map(e => e.code).filter(Boolean) ?? [])
            const silent = new Set(['ECONNREFUSED', 'ECONNRESET', 'EPIPE', 'ECONNABORTED'])
            const msg = err.message || ''
            if (codes.some(c => c && silent.has(c))) return
            if (msg.includes('socket hang up')) return
            console.error('[vite] api proxy error:', err.message)
          })
        },
      },
      '/ws': {
        target: 'ws://localhost:8089',
        ws: true,
        rewrite: (path) => path.replace(/^\/ws/, ''),
      },
    },
  },
  build: {
    outDir: 'dist',
  },
})
