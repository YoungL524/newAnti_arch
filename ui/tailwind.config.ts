/**
 * @file tailwind.config.ts
 * @brief 定义 Tailwind 扫描范围以及项目统一的视觉主题扩展。
 * @details content 路径必须覆盖所有 TSX 组件，否则生产构建会清除源码实际使用的样式类。
 */

import type { Config } from 'tailwindcss'

export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        'bg-titlebar': 'rgba(20, 24, 32, 0.92)',
        'bg-activity': '#161B22',
        'bg-sidebar': 'rgba(22, 27, 34, 0.82)',
        'bg-workspace': '#0F1218',
        'bg-statusbar': 'rgba(18, 22, 30, 0.90)',
        'accent-blue': '#4D8DFF',
      },
    },
  },
  plugins: [],
} satisfies Config

