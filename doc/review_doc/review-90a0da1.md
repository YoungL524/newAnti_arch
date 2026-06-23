# Review 90a0da1

## 结论

这一版质量是提升的，主要是在落实上一轮 `review-196a64e.md` 的三个建议：

1. `/api/status` 和 WS status 已经把 `openEndpoints > 0` 纳入 `acquisition/state` 判断。
2. `DeviceRegistry` 已经增加写入校验，非雷达设备不能携带 `endpointId`。
3. `AppController::shutdown()` 的生命周期语义已经补了注释。

当前没有发现必须立刻阻断的 P0/P1 问题。  
但状态模型还有两个值得继续打磨的点，否则后面进入多雷达、多端点、任务驱动后还会返工。

## 本轮已修好的点

### 1. 设备采集状态不再只看 WMC

位置：

- `core/web/WebApiController.cpp`
- `deviceStatusFromWmc()`
- `buildDeviceStatusArray()`
- `/api/status`
- WS status timer

现在 `acquisition` 已经变成：

```cpp
commActive && wmcAcquisitionActive(state)
```

这比之前只看 `RadarWorkModeController::state()` 更合理。  
当 core runtime 停止、通信端点关闭时，UI 不会继续显示“仍在采集”。

### 2. 设备台账不变量已经下沉到 Registry

位置：

- `core/business/DeviceRegistry.cpp`
- `DeviceRegistry::validateDeviceWrite()`
- `DeviceRegistry::create()`
- `DeviceRegistry::update()`

现在即使绕过 Web API 直接调用 `DeviceRegistry`，也不能写入：

- `kind != Radar`
- `endpointId != empty`

这是正确的。  
Web API 层负责友好错误提示，Registry 层负责最终兜底，职责分层更稳。

### 3. AppController 生命周期注释已补

位置：

- `core/AppController.h`

`shutdown()` 已明确不是运行态 stop，也不会关闭 SystemBus。  
这符合之前定下的原则：

- `stopRuntime()`：停止通信和 DPH
- `shutdown()`：释放 AppController 持有资源
- `SystemBus::shutdown()`：进程退出级关闭

## 仍需优化的问题

### P2. `openEndpoints > 0` 只是全局通信状态，不能长期当作单设备状态

位置：

- `core/web/WebApiController.cpp`
- `buildDeviceStatusArray()`
- `deviceStatusFromWmc()`
- `core/comm/CommunicationManager.h`

现状：

现在设备状态判断使用：

```cpp
const bool commActive = m_health().comm.openEndpoints > 0;
```

这在当前“单个可控雷达 endpoint = radar_cpu”的阶段可以接受。  
但它本质上是全局通信状态，不是某个设备绑定 endpoint 的状态。

未来如果出现：

- radar_cpu 打开，radar_xxx 关闭
- 多雷达多 endpoint
- 某个 endpoint 异常掉线
- 非雷达设备仍在线但雷达 endpoint 不在线

那么 `openEndpoints > 0` 会让某些设备状态被误判为可采集。

建议 Cursor 下一步修改：

1. 在 `CommunicationManager` 增加 endpoint 级查询：

```cpp
bool isEndpointOpen(const QString& endpointId) const;
```

2. `AppHealth` 可以继续保留总数，但设备状态合成不要只依赖总数。
3. `buildDeviceStatusArray()` 对每个设备使用自己的 `endpointId` 判断：

```cpp
const bool endpointOpen = !d.endpointId.isEmpty()
                       && isEndpointOpen(d.endpointId);
```

4. 对 legacy `deviceId == radar_cpu` 且 `endpointId` 为空的情况，可以临时映射到 `radar_cpu` endpoint。

短期可以不急，但进入多设备阶段前必须做。

### P2. `/api/status` 同一次响应内重复调用 `m_health()`，可能产生快照不一致

位置：

- `core/web/WebApiController.cpp`
- `/api/status`
- `connectWebPush()`
- `buildDeviceStatusArray()`

现状：

`/api/status` 中先调用了一次：

```cpp
auto health = m_health();
```

然后又调用：

```cpp
s["devices"] = buildDeviceStatusArray();
```

而 `buildDeviceStatusArray()` 内部再次调用：

```cpp
const bool commActive = m_health().comm.openEndpoints > 0;
```

这会让同一个 JSON 响应里的字段理论上来自两个不同 health 快照。  
例如极端情况下可能出现：

- `connected = true`
- 但 `devices[].acquisition = false`

或者相反。

建议 Cursor 修改：

1. 把 `buildDeviceStatusArray()` 改成接收一次性快照参数：

```cpp
QJsonArray buildDeviceStatusArray(const AppHealth& health) const;
```

2. `/api/status` 和 WS timer 都先取一次 `health`，再把同一个 `health` 传进去。
3. 这样状态响应就是一致快照，也减少重复查询。

这个改动很小，但会让状态模型更干净。

### P3. `deviceStatusFromWmc()` 名字还不够表达最终语义

位置：

- `core/web/WebApiController.cpp`

现在函数名是：

```cpp
deviceStatusFromWmc(...)
```

但它实际合成的是：

- WMC 状态
- 通信状态
- UI 展示状态

它已经不是单纯 “from WMC”。

建议后续可以改名为：

```cpp
buildRadarRuntimeDeviceStatus(...)
```

或者：

```cpp
composeRadarDeviceStatus(...)
```

这不是功能 bug，只是为了避免后续维护者误以为设备状态只来自 WMC。

## 架构判断

这一版仍然符合当前架构方向：

- DeviceRegistry 是设备台账 source of truth
- endpoint 是通信端点，不是业务设备
- 非雷达设备不能绑定通信 endpoint
- Web 常驻，runtime stop 不关闭 Bus/Web
- UI 状态由 core 合成，不由前端自行推断

目前最需要继续压实的是“状态合成层”。  
后续建议明确区分四种状态：

1. core runtime 状态：上位机运行态是否启动
2. communication 状态：endpoint 是否打开
3. radar work mode 状态：雷达业务工作模式
4. UI display 状态：前端最终展示的设备状态

现在代码已经开始区分 2 和 3，但还没有完全建模 1 和 4。  
这不影响当前运行，但会影响后面任务驱动、多设备和历史数据归属。

## 代码质量评估

当前代码质量维持在 `B+`，并且比上一版更稳。

加分点：

- 修复范围克制，没有把 Web/Bus/runtime 生命周期重新搅乱。
- Registry 层开始承担业务不变量，架构边界更硬。
- 状态合成开始从“只看 WMC”转向“WMC + 通信状态”。

扣分点：

- `WebApiController` 仍然偏重，状态合成、路由、命令响应、设备 DTO 都挤在一起。
- endpoint 级状态还没进入 health/device DTO。
- status 快照还不是严格单次采样。

建议下一轮 Cursor 不要大拆 WebApiController，先做两个小改：

1. `buildDeviceStatusArray(const AppHealth& health)`
2. `CommunicationManager::isEndpointOpen(endpointId)`

这两个补完后，再考虑 route module 拆分会更顺。
