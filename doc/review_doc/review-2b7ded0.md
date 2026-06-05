# Review for 2b7ded0

Full commit: `2b7ded068dc4b80585a040627dad1a523b730a69`

## Findings

### P0 - `DeviceRegistry.h` uses `std::optional` but does not include `<optional>`

- File: `core/business/DeviceRegistry.h`
- Line: 52

`DeviceRegistry::get()` returns `std::optional<DeviceInfo>`, but the header does not include `<optional>`. Because this is a public header, it should be self-contained. It may compile only accidentally if another include happens to pull `<optional>` first, but it is not safe and can fail depending on include order/compiler.

Suggested fix for Claude Code:

```cpp
#include <optional>
```

Add it to `DeviceRegistry.h`. Also remove `<memory>` from that header if it remains unused.

### P1 - `PUT /api/devices/{id}` and `DELETE /api/devices/{id}` do not use the path id

- File: `core/web/WebApiController.cpp`
- Lines: 149-176
- File: `core/web/WebService.cpp`
- Lines: 279-310

The registered routes are prefix routes:

```cpp
handlePutPrefix("/api/devices/", ...)
handleDeletePrefix("/api/devices/", ...)
```

but `JsonHandler` receives only the parsed JSON body. The path suffix `{id}` is never passed to the handler, so `PUT /api/devices/radar_1` only works if the body also contains `"id": "radar_1"`, and `DELETE /api/devices/radar_1` only works if the request body contains `"id"`.

That contradicts the documented API shape in `doc/business/设备台账与端点映射.md`, where the id is part of the path. It also makes `DELETE` awkward for normal clients, because many clients send no JSON body for DELETE.

Suggested fix for Claude Code:

1. Extend prefix routing to pass path params or at least the matched suffix:

```cpp
using PrefixJsonHandler = std::function<QJsonObject(const QString& suffix, const QJsonObject&)>;
```

2. For `/api/devices/{id}`, URL-decode the suffix and use it as the canonical id.
3. For `PUT`, reject body/path id mismatch if both exist.
4. For `DELETE`, use the path id and do not require a body.
5. Add tests/manual smoke checks for:
   - `PUT /api/devices/radar_1` with body missing id;
   - `PUT /api/devices/radar_1` with mismatched body id;
   - `DELETE /api/devices/radar_1` with empty body.

### P1 - Frontend loads backend devices but create/update/delete still only mutate local React state

- File: `ui/src/App.tsx`
- Lines: 28-72, 129-172

Startup now loads `/api/devices`, which is the right architectural direction. But `addDevice`, `removeDevice`, `addSubDevice`, `removeSubDevice`, `updateDevice`, and `updateSubDevice` still only update local React state. They do not call the new backend APIs.

The business document says:

```text
前端添加/编辑/删除设备 -> 调对应 API -> 成功后更新本地状态
```

Current behavior violates that source-of-truth decision: a user can create/edit/delete devices in the UI and see local changes, but refresh loses them because the backend registry was never updated.

Suggested fix for Claude Code:

1. Move device CRUD into a small frontend API helper, for example `ui/src/lib/devicesApi.ts`.
2. On create:
   - generate or receive id,
   - POST `/api/devices`,
   - only update React state after success.
3. On update:
   - PUT `/api/devices/{id}`,
   - update local state after success.
4. On delete:
   - DELETE `/api/devices/{id}`,
   - update local state after success.
5. Preserve local fallback only when backend is unavailable, but make that state explicit in UI or logs.

### P2 - `DeviceRegistry::update()` reports success even when no row was updated

- File: `core/business/DeviceRegistry.cpp`
- Lines: 101-121

`update()` executes an `UPDATE ... WHERE id = ?` and returns whether SQL execution succeeded. SQL execution can succeed even if zero rows matched. That means updating a nonexistent device returns `{ ok: true }` to Web.

Suggested fix for Claude Code:

1. Either call `get(id)` before update and return false if absent, or extend `DataStore` to expose affected row count.
2. Do the same consideration for `remove(id)`, because deleting a nonexistent id can also report success.
3. API should return `404`-style semantic failure or at least `{ ok: false, error: "device not found" }`.

### P2 - Device API has no validation for enum/range fields

- File: `core/web/WebApiController.cpp`
- Lines: 128-176

Fields such as `kind`, longitude/latitude, `facesJson`, endpoint id, and linked radar id are accepted without validation. For a registry that becomes source of truth, this will create bad data quickly.

Suggested fix for Claude Code:

Start with minimal validation:

- `kind` must be one of Radar/Opto/Level/Gps;
- longitude in `[-180, 180]`;
- latitude in `[-90, 90]`;
- `facesJson` must parse as an array for radar devices;
- if `linkedRadarId` is set, referenced radar should exist;
- if `endpointId` is set, it should match a configured endpoint if that information is available.

## Architecture Notes

This commit is on the right architectural track: device information starts moving from frontend local state into a backend `DeviceRegistry`, which matches the newly agreed business/application architecture.

The main gap is that the implementation is only halfway across the boundary. Backend has a registry and list/create/update/delete endpoints, but the frontend still treats local React state as the write path. The next increment should complete the source-of-truth shift rather than adding more fields.

## Verification

I did not run local CMake because `cmake` is not available in the current PATH. Review is based on source inspection and the pulled diff.
