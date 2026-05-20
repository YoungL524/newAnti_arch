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
