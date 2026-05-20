/** Protocol types matching C++ HttpServer API */

export interface StatusResponse {
  ok: boolean
  acquisition: boolean
  state: string
  connected: boolean
  uptime: number
  endpoints: number
  dbConnected: boolean
}

export interface WsStatusMessage {
  type: 'status'
  acquisition: boolean
  state: string
  connected: boolean
  upTime: number
}

export type WsMessage = WsStatusMessage
