# Review for b9ce408

Full commit: `b9ce40881a68b930913b69af8616802712cbc02d`

## Findings

No blocking issues found in this pull.

The previous review items were addressed:

- invalid `Content-Length` now checks `toInt(&ok)` and rejects negative values;
- pipelining tests now parse the second request instead of only checking `startsWith`;
- chunked trailer and malformed chunk CRLF coverage remains in place.

## Minor Notes

### P3 - Pipelined POST test does not assert full consumption of the second request

- File: `doc/test/src/tst_HttpParser.cpp`
- Around the `parse_POST_consumed_and_second_request()` test

The chunked pipelining tests assert `r2->consumed == rest.size()`, but the plain `Content-Length` pipelining test only checks method/path. This is not a production bug; it is just a small consistency gap in the test.

Suggested fix for Claude Code:

Add:

```cpp
QCOMPARE(r2->consumed, static_cast<size_t>(rest.size()));
```

to the plain POST pipelining test as well.

## Optimization Directions To Evaluate

### 1. Add HTTP connection input limits and idle timeout

Current area:

- `core/web/WebService.cpp`
- `core/web/HttpParser.cpp`

Why it matters:

`WebService::onHttpReadyRead()` appends all bytes into `m_buffers[sock]` and waits for a complete request. There is no max header size, max body size, max buffered bytes per socket, or idle timeout for incomplete requests. Even if this is mostly local UI traffic, one stalled or oversized client can keep memory and socket state alive indefinitely.

Suggested direction for Claude Code:

1. Add constants such as:
   - `kMaxHeaderBytes`, for example 16 KiB;
   - `kMaxBodyBytes`, based on actual REST needs;
   - `kMaxBufferedBytes`, for total per-socket pending data.
2. Let `tryParseRequest()` distinguish incomplete vs malformed vs too large, or add a pre-parse guard in `WebService`.
3. Add a per-socket idle timer or last-read timestamp for incomplete requests.
4. Tests:
   - oversized header is rejected;
   - oversized `Content-Length` is rejected before buffering forever;
   - incomplete request is closed after timeout.

Architecture fit:

Keep protocol framing rules in `HttpParser`, but keep socket lifecycle and timeout policy in `WebService`.

### 2. Make Web push subscriptions lifecycle-managed

Current area:

- `core/AppController.cpp`
- `core/AppController.h`
- `core/bus/SystemBus.cpp`

Why it matters:

`connectWebServiceSignals()` subscribes to `SystemBus::subscribeByTarget(UI_OUTPUTWIDGET, ...)`, but the returned subscription id is not stored. `SystemBus` supports `unsubscribe()`, so the architecture already has the mechanism. Storing and releasing this id would make Web service restart/stop behavior cleaner and reduce risk of callbacks capturing stale `web::WebService*`.

Suggested direction for Claude Code:

1. Add a member such as `uint32_t m_webTrackSubscriptionId = 0;`.
2. Store the id returned by `subscribeByTarget`.
3. Unsubscribe it in Web service shutdown / `AppController::stop()` before destroying `m_webService`.
4. Consider clearing `Logger::instance().setExternalCallback(...)` on shutdown if the logger callback can outlive the Web service.

Architecture fit:

This keeps `AppController` as composition root responsible for cross-component wiring and lifecycle, without pushing bus knowledge into `WebService`.

### 3. Split Web API assembly out of `AppController`

Current area:

- `core/AppController.cpp`, especially REST route registration and WS message assembly

Why it matters:

`AppController` currently owns initialization, database schema, communication setup, processor/router assembly, business controller wiring, REST route registration, WebSocket track/log/status payload assembly, and health reporting. It is working, but it is becoming a broad composition root plus API presenter.

Suggested direction for Claude Code:

1. Keep `AppController` as composition root.
2. Extract a small Web-facing adapter, for example `WebApiController` or `WebPresenter`, that receives references to:
   - `web::WebService`,
   - `RadarWorkModeController`,
   - health-report provider,
   - logger/system bus hooks.
3. Move JSON response/message construction there.
4. Keep `AppController` responsible for creating it and managing its lifetime.

Architecture fit:

This preserves the current layering while reducing `AppController` churn. Future UI/API changes would touch the adapter rather than the whole application composition root.

## Verification

I could not run CMake locally because `cmake` is not available in the current PATH. Review is based on source inspection and the pulled diff.
