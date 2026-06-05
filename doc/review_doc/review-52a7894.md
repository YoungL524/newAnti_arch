# Review for 52a7894

Full commit: `52a789453f61e6152cef3d9dfd69d532b43cceab`

## Findings

### P0 - `WebApiController.cpp` does not compile because `AppHealth` is incomplete and `QDebug` is not included

- File: `core/web/WebApiController.h`
- Lines: 8-20
- File: `core/web/WebApiController.cpp`
- Lines: 1-10, 24-31, 102-108, 152-157

The attached build log shows the current failure:

- `invalid use of incomplete type 'struct AppHealth'`
- `return type 'struct AppHealth' is incomplete`
- `invalid use of incomplete type 'class QDebug'`

`WebApiController.h` only forward declares `struct AppHealth`, but it defines:

```cpp
using HealthProvider = std::function<AppHealth()>;
```

and `WebApiController.cpp` calls `m_health()` and reads fields such as `health.comm.openEndpoints`. Returning `AppHealth` by value through `std::function` requires the full type at the call site. A forward declaration is not enough.

`WebApiController.cpp` also uses `qWarning()` and `qDebug()` but does not include `<QDebug>`, so MinGW only sees Qt's forward declaration of `QDebug`.

Suggested fix for Claude Code:

1. Prefer moving `AppHealth` and `WebServiceResult` out of `AppController.h` into a small shared header, for example `core/AppTypes.h` or `core/app/AppTypes.h`.
2. Include that shared header from both `AppController.h` and `web/WebApiController.h`.
3. Add `#include <QDebug>` to `WebApiController.cpp`.
4. Keep `AppController` and `WebApiController` from including each other directly, otherwise the new Web API split will create a circular dependency.

Minimal compile-only fix:

```cpp
// WebApiController.cpp
#include "AppController.h"
#include <QDebug>
```

But the cleaner architecture fix is a shared app types header, because `WebApiController` should not need the whole `AppController` class just to know the health DTO shape.

### P1 - Web API shutdown is skipped when Web service was started but `AppController::start()` was not

- File: `core/AppController.cpp`
- Lines: 223-246
- File: `core/AppController.cpp`
- Lines: 582-592

`startWebService()` creates `m_webApi`, registers callbacks, subscribes to `SystemBus`, installs the logger callback, and starts a status timer. But `AppController::stop()` immediately returns when `m_running` is false:

```cpp
if (!m_running) {
    return;
}
```

So if the app starts Web service before or independently of `AppController::start()`, destructor-time `stop()` will not call `m_webApi->shutdown()`. That leaves callbacks installed longer than intended, including callbacks that capture raw `web::WebService*`.

Suggested fix for Claude Code:

1. Move Web API shutdown before the `m_running` early return, or split shutdown into independent sections.
2. Make `WebApiController` destructor call `shutdown()` as a defensive backstop.
3. Consider resetting `m_webApi` before resetting/stopping `m_webService`, so queued callbacks are disconnected before the transport object goes away.

Architecture note:

This keeps the composition root responsible for lifecycle, but makes the lifecycle independent from the core communication `m_running` flag.

### P2 - `413 Payload Too Large` response serializes with generic status text

- File: `core/web/WebService.cpp`
- Lines: 243-248
- File: `core/web/HttpParser.cpp`
- Lines: 215-220

The new body-size guard sends:

```cpp
sendError(sock, 413, QStringLiteral("Payload Too Large"), keepAlive);
```

but `statusText()` does not include 413, so the wire response becomes:

```text
HTTP/1.1 413 Error
```

Suggested fix for Claude Code:

Add `case 413: return "Payload Too Large";` to `statusText()`, and add a small serialization test if possible.

## Architecture Notes

The direction of this commit is good:

- `WebApiController` is the right kind of adapter to reduce `AppController` growth.
- `SystemBus` subscription id tracking is the right lifecycle mechanism.
- HTTP buffer limits and idle timeout are valuable hardening.

The main issue is dependency shape: `WebApiController` needs DTO types from the app layer, but should not depend on the whole `AppController`. A small shared type header is the cleanest way to preserve the architecture.

## Verification

The attached build log already verifies a compile failure in `core/web/WebApiController.cpp`. I did not run local CMake because `cmake` is not available in the current PATH.
