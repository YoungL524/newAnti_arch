/**
 * @file main.tsx
 * @brief 将 React 应用挂载到页面根节点并启用开发期严格模式。
 * @details 全局样式在此入口加载；核心连接、任务与航迹状态由 App 及其 Provider 初始化。
 */

import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import App from './App'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)

