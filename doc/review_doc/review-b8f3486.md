# Review b8f3486

## 结论

这一版方向是对的：`TrackDisplayManager` 已经不再直接依赖 WebSocket，航迹显示数据开始以 `SOURCE_TRACK_DISPLAY` 的形式进入 `SystemBus`，再由 `TrackDisplayWebBridge` 转换成 Web DTO 推给 React Web。这个链路基本符合我们刚定下来的架构原则：

`RadarDataProcessor -> TrackDisplayManager -> SystemBus -> Web/WebSocket Adapter -> React Web`

但当前实现还有几个必须修的点，尤其是编译失败和 `DataPacket/BusEnvelope` 的大对象载荷设计。建议 Cursor 按下面顺序处理。

## 必须修改

### 1. TrackDisplayWebBridge 的 Qt MOC 编译风险需要先修

位置：

- `core/web/TrackDisplayWebBridge.h:7`
- `core/web/TrackDisplayWebBridge.h:24`
- `core/business/TrackTypes.h`

当前头文件里只 forward declare 了：

```cpp
struct TrackDisplaySnapshot;
```

但 `Q_OBJECT` 类的 slot 使用了按值参数：

```cpp
void deliverSnapshot(TrackDisplaySnapshot snap);
```

这很容易导致 `mocs_compilation.cpp.obj` 编译失败。`build_err.txt` 里当前只保留了 make 顶层错误，没有真实编译器诊断，但失败点正好是 `mocs_compilation.cpp.obj`，和这里高度吻合。

修改建议：

- 在 `TrackDisplayWebBridge.h` 里直接 `#include "TrackTypes.h"`，不要只 forward declare。
- 或者更好：不要让 Qt slot 按值接收大型业务对象，改成队列投递 lambda / shared pointer，见第 4 点。

验收标准：

- `cmake --build build --target NewAntiDrone_core` 能通过。
- 如果仍失败，把完整编译错误写入 `build_err.txt`，不要只保留 make 的最后几行。

### 2. BusEnvelope 现在是航迹专用补丁，不是通用 DataPacket 优化

位置：

- `core/bus/BusEnvelope.h:15`
- `core/bus/BusEnvelope.h:17`
- `core/bus/BusEnvelope.h:20`
- `core/bus/BusEnvelope.h:26`
- `core/bus/SystemBus.cpp:429`
- `core/bus/SystemBus.cpp:431`

当前 `BusEnvelope` 的设计是：

```cpp
DataPacket packet;
std::shared_ptr<const TrackDisplaySnapshot> trackSnapshot;
```

然后通过 `thread_local const BusEnvelope* s_dispatching`，在回调执行期间用 `BusEnvelope::trackSnapshotFrom(pkt)` 反查大对象。

这能解决当前航迹 snapshot 过大、塞不进 `DataPacket::payload.rawData` 的问题，但它有几个架构隐患：

- `BusEnvelope` 被写死成 `TrackDisplaySnapshot` 专用，后面任务状态、历史回放、告警批量推送等大对象又会重复开洞。
- 订阅回调签名还是 `const DataPacket*`，真实载荷靠 thread-local side channel 获取，调用方无法从接口层看出这个包是否携带对象载荷。
- `setDispatchContext/clearDispatchContext` 不是 RAII。如果后面有人在 `invokeCallbacks` 外层加 return 或新增异常路径，很容易遗留错误上下文。

建议把这次改动升级成一个稳定的总线载荷模型，而不是只给航迹开特例。

推荐方案：

```cpp
enum class BusPayloadKind {
    RawDataPacket,
    TrackDisplaySnapshot,
    // 后续扩展：TaskStateChanged, RuntimeSettingsChanged, AlarmBatch...
};

struct BusEnvelope {
    DataPacket header;
    BusPayloadKind payloadKind = BusPayloadKind::RawDataPacket;
    std::shared_ptr<const void> objectPayload;

    template <typename T>
    std::shared_ptr<const T> payloadAs() const;
};
```

同时给总线增加面向 envelope 的订阅接口，至少内部 callback 应该能拿到 `BusEnvelope`：

```cpp
using BusEnvelopeCallback = std::function<void(const BusEnvelope&)>;
```

短期可以保留旧的 `DataPacket*` 订阅接口做兼容，但 Web/业务新链路应该走 envelope callback。

最低限度修改：

- 不再暴露 `trackSnapshotFrom(const DataPacket*)` 这种隐式接口。
- 如果暂时保留 thread-local，也要改成 RAII guard，确保 `clearDispatchContext()` 一定执行。
- 文件注释要明确：`DataPacket` 是路由头/小包载荷，`BusEnvelope` 是总线消息实体，大对象通过共享只读 payload 挂载。

### 3. SystemBus 派发上下文需要异常安全

位置：

- `core/bus/SystemBus.cpp:429`
- `core/bus/SystemBus.cpp:430`
- `core/bus/SystemBus.cpp:431`

当前代码：

```cpp
BusEnvelope::setDispatchContext(&envelope);
invokeCallbacks(callbacksToInvoke, dataPacket);
BusEnvelope::clearDispatchContext();
```

虽然 `invokeCallbacks()` 内部 catch 了回调异常，但这个模式本身仍然脆弱。建议改成作用域 guard：

```cpp
class DispatchContextGuard {
public:
    explicit DispatchContextGuard(const BusEnvelope* env) { BusEnvelope::setDispatchContext(env); }
    ~DispatchContextGuard() { BusEnvelope::clearDispatchContext(); }
};
```

如果按第 2 点改成 envelope callback，这个 guard 可以直接删除。

### 4. TrackDisplaySnapshot 当前仍发生多次深拷贝，抵消了 shared_ptr 的意义

位置：

- `core/business/TrackDisplayManager.cpp:157`
- `core/business/TrackDisplayManager.cpp:159`
- `core/web/TrackDisplayWebBridge.cpp`
- `core/web/TrackDisplayWebBridge.h:24`

当前链路大概是：

1. `snapshot()` 返回一个完整 `TrackDisplaySnapshot`。
2. `deliverSnapshot(const TrackDisplaySnapshot& snap)` 里 `make_shared<TrackDisplaySnapshot>(snap)` 再复制一次。
3. `TrackDisplayWebBridge::onBusPacket()` 里 `Q_ARG(TrackDisplaySnapshot, *snapPtr)` 又按值投递一次。
4. `deliverSnapshot(TrackDisplaySnapshot snap)` slot 再按值接收。

这会让“大对象走 shared_ptr”只解决了固定 payload 数组问题，没有真正解决 snapshot 拷贝成本。

建议：

- `TrackDisplayManager::snapshot()` 可以先保持返回值不动，依赖移动/RVO。
- `deliverSnapshot()` 最好接收右值或直接构造 shared snapshot：

```cpp
void TrackDisplayManager::deliverSnapshot(TrackDisplaySnapshot&& snap);
```

- Web bridge 队列投递时传 `std::shared_ptr<const TrackDisplaySnapshot>`，不要传 `TrackDisplaySnapshot` 值。
- 如果要经过 Qt queued invocation，需要 `Q_DECLARE_METATYPE(std::shared_ptr<const TrackDisplaySnapshot>)` 或者使用 lambda capture：

```cpp
QMetaObject::invokeMethod(this, [guard, snapPtr]() {
    if (guard) {
        guard->pushSnapshot(*snapPtr);
    }
}, Qt::QueuedConnection);
```

这样可以避免 MOC 对 `TrackDisplaySnapshot` 完整类型的依赖，也避免大对象重复复制。

### 5. 不要提交构建日志和 drawio 备份文件

位置：

- `build_err.txt`
- `doc/.$系统架构.drawio.bkp`

这两个文件不应该进入仓库：

- `build_err.txt` 是本地构建过程产物，且目前内容不完整，只有 make 顶层失败信息。
- `doc/.$系统架构.drawio.bkp` 是 drawio/Office 类工具生成的临时备份文件。

修改建议：

- 从 Git 中移除这两个文件。
- `.gitignore` 增加：

```gitignore
build_err.txt
doc/.$*.bkp
*.drawio.bkp
```

如果确实需要保留构建错误，建议放到 review 文档里，只记录和本次 review 有关的完整错误摘要。

## 架构验收

### 已经做对的部分

- `TrackDisplayManager` 不再直接持有 WebSocket，也不直接向 UI 层广播。
- `TrackDisplayWebBridge` 被放在 `core/web` 下，职责是总线事件到 Web DTO 的适配，这个边界是合理的。
- `SystemBus::dispatchToSubscribers()` 收集回调时持锁，调用回调时已经离开锁，这点是对的，暂时没有明显的 unsubscribe 死锁问题。
- `TrackDisplaySnapshot` 作为 UI 显示协议，不再把原始协议点直接暴露给前端，这符合“UI 订阅显示态，不订阅原始协议态”的原则。

### 仍需坚持的架构边界

- React Web 获取航迹数据，只能通过后端 Web API/WebSocket 输出的总线事件结果，不能直接依赖 `TrackDisplayManager`。
- `TrackDisplayManager` 是业务显示态管理，不负责航迹起批、消失判定等数据处理职责。
- `DataPacket` 不应继续膨胀成所有业务对象的巨大 union；它更适合作为总线消息头、小包载荷和路由信息。
- 大对象、复杂 DTO、批量 snapshot 应该通过 `BusEnvelope` 或同级消息模型统一承载。

## 建议 Cursor 的修改顺序

1. 先修 `TrackDisplayWebBridge` 的 MOC 编译问题，确保 core 能编译。
2. 清理 `build_err.txt` 和 `doc/.$系统架构.drawio.bkp`，补 `.gitignore`。
3. 把 `BusEnvelope` 从 `TrackDisplaySnapshot` 专用字段升级为通用 typed payload。
4. 给 `SystemBus` 增加 envelope callback，旧的 `DataPacket*` callback 暂时兼容。
5. 把 `TrackDisplayWebBridge` 改为接收 shared snapshot，避免 queued slot 深拷贝。
6. 编译通过后，再更新 `doc/architecture/05-核心流程.drawio`，明确 UI 航迹链路是：

```text
CommunicationManager
  -> RadarDataProcessor
  -> TrackDisplayManager
  -> SystemBus(BusEnvelope: TrackDisplaySnapshot)
  -> TrackDisplayWebBridge/WebSocket
  -> React Web
```

## 备注

单航迹点数不设上限是业务侧要求，不要把它当 bug 回滚。当前 `kMaxSnapshotPoints = 8000` 是全局 snapshot 输出上限，和“单航迹完整保留”不是同一件事。后续如果性能压力变大，应该做全局水位、snapshot 频率、前端视窗裁剪或历史数据落库策略，而不是重新给单航迹缓存加硬上限。
