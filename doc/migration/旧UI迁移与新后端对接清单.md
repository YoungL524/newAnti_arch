# 旧 UI 迁移与新后端对接清单

## 当前结论

`ui/` 已从旧仓库提取为新仓库的视觉与交互基线，React、地图、任务/设备编辑、航迹增量归并和历史页面均可继续复用。迁移不代表旧 HTTP/WebSocket 后端已经进入新架构；浏览器永远位于序列化边界之外，不能直接访问 `CoreDataDomain`。

## 与固定架构的对应关系

```text
CoreDataDomain::trackTopic
        ^ readAfter(webCursor)
        |
   TrackWebSocketService（已实现首版）
        |
        +-- 初始 snapshot + 有界 delta / removal --> WebSocket --> React

UI Command
        --> ApplicationController
        --> DeviceController（唯一 owner）
        --> Per-Device FIFO --> ProtocolAdapter --> Device
```

- `Qt signal/slot` Doorbell 只存在于进程内 `Topic -> WebReader/ExecutionLane` 调度边界，不跨 WebSocket 暴露。
- `snapshot/delta` 是 WebReader 的序列化输出，不是第二份 Topic 历史；浏览器只保留当前显示态。
- `/api/start`、`/api/stop`、`/api/reset` 和航迹交互均属于独立 Command 控制平面，HTTP 成功只能表示 owner 接受请求；ACK 后状态再由 State/Event 输出。

## 接入前必须收敛的合同

| 旧 UI 接口 | 新后端 owner | 当前处理 |
|---|---|---|
| `/api/devices` | `DeviceConfigService` 的 HTTP 边界 | 保留页面，待定义 JSON 与 revision 冲突响应 |
| `/api/tasks` | `TaskService` 的 HTTP 边界 | 保留页面；删除必须是软删除 |
| `/api/start|stop|reset` | `ApplicationController -> DeviceController` | 保留按钮，禁止映射到普通 Topic |
| `/api/status` | `LatestState<DeviceState/TaskState/SystemState>` 的 Web/HTTP Reader | State 类型合同已定义，待正式 owner 接入 |
| `/api/tracks/history` | SQLite 查询服务 | 保留页面，分页字段需与六表 Schema 对齐 |
| `/api/system-events` | SQLite `system_events` 查询服务 | 保留页面 |
| 8089 `tracks` WebSocket | `TrackWebSocketService` | 已接通初始快照 + 有界尾迹增量 upsert/removal |

## 已识别但暂不伪造实现的问题

1. Web 航迹键现已包含 `acquisitionId + deviceId + trackId + pointIndex`，避免设备重启后复用 `trackId` 串轨。
2. `TrackWebSocketService` 只从紧凑 TrackPacket 生成地图所需字段；旧 `x/y/z/rcs` 没有重新塞回权威事实。
3. `DeviceState/TaskState/SystemState` 类型及门禁已定义，但正式状态 owner 仍待接入，因此 `/api/status` 尚无正式实现，不得用硬编码联试视图冒充 State。
4. 地图 Key 与安全密钥从 `ui/.env.example` 创建本机 `.env`；真实密钥不提交仓库。
5. 一次性硬编码 HTTP 联试服务已经删除；创建任务、启动/停止和设备配置必须等待正式 owner 接入。

## 验收命令

```powershell
cd ui
npm ci
npm test
npm run build
```
