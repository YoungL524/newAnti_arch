import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    strictPort: true,
    // WS 由前端直连 core:8089，不经 Vite 代理（刷新时代理层 ECONNRESET 无法抑制）
    proxy: {
      '/api': 'http://localhost:8088',
    },
  },
  build: {
    outDir: 'dist',
  },
})
