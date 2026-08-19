/**
 * @file global.d.ts
 * @brief 声明浏览器全局对象上由地图脚本等外部运行时注入的能力。
 * @details 这里只描述类型，不负责加载第三方脚本；使用方仍需处理对象尚未注入的情况。
 */

interface Window {
  /** AMap loader security configuration injected by the hosting environment. */
  _AMapSecurityConfig?: {
    /** Provider-issued JavaScript security token. */
    securityJsCode?: string
    /** Optional proxy endpoint used instead of a client-side security token. */
    serviceHost?: string
  }
}
