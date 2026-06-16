# Review for a44cb8d

Full commit: `a44cb8d3e9a12618978e0046efc1f1cac873bf68`

## Findings

### P1 - Device update path is patch-like on the frontend but full-replace on the backend

- File: `ui/src/App.tsx`
- Lines: 179-184
- File: `ui/src/lib/devicesApi.ts`
- Lines: 21-29
- File: `core/web/WebApiController.cpp`
- Lines: 174-185

The frontend update calls send only partial fields:

```ts
const payload: any = { ...updates }
apiUpdateDevice(id, payload)
```

but the backend parses that partial body into a full `DeviceInfo`, validates it as if every required field exists, then calls `DeviceRegistry::update(d)`.

Consequences:

- Editing only `lng` or `faces` sends no `name`, so backend returns `id 和 name 不能为空`.
- Even if validation were relaxed, missing fields would become defaults (`kind=Radar`, `lat=0`, `brand=""`, etc.) and overwrite the existing row.

This breaks device editing and conflicts with the source-of-truth goal: backend registry must own complete device state, and updates must have clear semantics.

Suggested fix for Claude Code:

Choose one of these contracts and make both sides match:

1. **Full replace (`PUT`)**
   
   - Frontend must merge `updates` with the current complete device object and send the full DTO.
   - Backend keeps full-object validation.
   - API docs should say `PUT /api/devices/{id}` requires a full device payload.

2. **Partial update (`PATCH`)**
   
   - Add `Method::PATCH` and `handlePatchPrefix`.
   - Backend loads existing device, applies only provided fields, validates the merged result, then updates.
   - Frontend keeps sending partial updates.

For the current UI, option 2 is more ergonomic. If Claude wants a smaller change, option 1 is acceptable but must update `App.tsx` to send the full merged device.

### P1 - Device CRUD updates UI optimistically and ignores backend failure

- File: `ui/src/App.tsx`
- Lines: 131-184

Create/delete/update mutate React state before the API call completes and then swallow failures:

```ts
createDevice(...).catch(() => {})
deleteDevice(id).catch(() => {})
apiUpdateDevice(id, payload).catch(() => {})
```

If backend rejects validation, database is unavailable, or network fails, the UI shows a state that is not persisted. On refresh, the device list snaps back. This is especially risky now that backend is supposed to be the source of truth.

Suggested fix for Claude Code:

1. Make device operations async.
2. Call backend first.
3. Only update local state after success.
4. On failure, show/log an error and keep local state unchanged.
5. If optimistic UI is desired later, implement rollback explicitly.

### P1 - Status/control APIs still use hard-coded `radar_cpu`, not DeviceRegistry ids

- File: `core/web/WebApiController.cpp`
- Lines: 33-56, 75-98, 257-271

`/api/devices` now exposes backend registry devices, but `/api/status`, `/api/start`, `/api/stop`, and WS status still use a single hard-coded device id:

```cpp
dev["id"] = "radar_cpu";
...
if (v.toString() == "radar_cpu") { ok = true; break; }
```

This means a user can create devices in the registry, but those device ids do not participate in status aggregation or start/stop control. The architecture says device id is the business identity, so the control/status API must eventually use registry ids.

Suggested fix for Claude Code:

For the next increment, do not attempt full multi-device state machine yet. But make the mismatch explicit:

1. Add a mapping function from registry device id to current backend work-mode controller target.
2. Return registry devices in `/api/status`, at least with `Offline/Standby` state.
3. For now, only allow start/stop for the device bound to `radar_cpu`, and return a clear error for unsupported ids.
4. Document this as transitional behavior.

### P2 - Prefix route suffix is not URL-decoded

- File: `core/web/WebService.cpp`
- Lines: 279-288

`suffix` is extracted from the raw path but not URL-decoded. Frontend uses `encodeURIComponent(id)`, so any id containing spaces, slash-like escaped characters, Chinese text, etc. will not round-trip.

Suggested fix for Claude Code:

Use `QUrl::fromPercentEncoding(suffix.toUtf8())` when passing suffix to prefix handlers.

### P2 - `fetchDevices()` helper is added but startup still hand-rolls fetch parsing

- File: `ui/src/lib/devicesApi.ts`
- Lines: 3-8
- File: `ui/src/App.tsx`
- Lines: 28-72

`devicesApi.ts` adds `fetchDevices()`, but `App.tsx` still performs raw `fetch('/api/devices')` and maps `any` directly. This is not a bug, but it is a missed consolidation. The new helper should be the only place that knows the device API shape.

Suggested fix for Claude Code:

Move backend DTO -> `RadarDevice` / `SubDevice` mapping into `devicesApi.ts` or a nearby mapper module, then let `App.tsx` consume typed results.

## Architecture Notes

This commit fixes most findings from `review-2b7ded0`: `<optional>` is included, prefix handlers receive suffixes, nonexistent update/delete are rejected, validation exists, and frontend now calls CRUD APIs.

The remaining architectural gap is update semantics. DeviceRegistry is becoming the backend source of truth, so the API must avoid ambiguous "partial body into full row" behavior. Clear `PUT full replace` vs `PATCH partial update` semantics should be fixed before adding more device fields.

## Verification

I did not run local CMake or frontend build because the local environment does not expose the full build toolchain in PATH. Review is based on source inspection and the pulled diff.
