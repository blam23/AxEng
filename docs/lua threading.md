## Background Lua tasks without bulk table copies

Declare `"threads"` in the project's `required_permissions` and launch with
`--allow-threads`.


e.g:
```powershell
AxEng.exe -cxr --in tower_mancer --out "$env:TEMP\AxTowerMancer" --allow-threads --allow-io --allow-os
```

(only use `-x` with an output directory you intend to erase)

`bg.submit(script_name, input)` executes a registered script in a worker-owned Lua
context. The script must return a function accepting `(request, task)`:

```lua
-- Register this script as "calculate" in project.json.
return function(request, task)
    local values = bg.number_buffer(request.count)
    for i = 1, request.count do
        if task:cancelled() then return {} end
        values:set(i, i * request.multiplier)
    end
    return { values = values }
end
```

On the main thread, submit once and poll from an update callback:

```lua
local task = bg.submit("calculate", { count = 1024, multiplier = 2 })
-- In a later update:
if task:status() == "succeeded" then
    local result = task:take_result()
    print(result.values:get(1))
elseif task:status() == "failed" then
    log.error(task:error())
elseif task:status() == "cancelled" then
    -- Discard this request.
end
```

Task status is `queued`, `running`, `succeeded`, `failed`, or `cancelled`.
`take_result()` is non-blocking and succeeds exactly once after success; misuse
raises a Lua error. `cancel()` requests cooperative cancellation, and task code
should check `task:cancelled()` in long loops. There is no main-thread wait API.
Shutdown cancels queued work and joins the worker; uncooperative scripts can delay
shutdown. Queued cancellation skips script execution when the queue drains.

Inputs and results are flat records with at most 32 string keys (1-128 bytes).
Values may be booleans, integers, finite numbers, strings (up to 4,096 bytes each),
or native buffer handles. Nil fields are absent. Nested tables, functions and
arbitrary userdata are rejected. Native input buffers must already be sealed;
result buffers are sealed automatically when published. A sealed buffer can never
be modified, including through an alias retained by its producer.

- `bg.number_buffer(count)`: contiguous doubles, with 1-based `get`, `set`,
  `size`, `seal` and `sealed` methods.
- `bg.sprite_buffer(count)`: native sprite records, with `size`, `seal`, `sealed`
  and `set(index, x, y, z, rx, ry, rw, rh, scale_x, scale_y, tint_vec4)`.
  Regions use texture-local pixels. Rotation defaults to zero and coordinates
  are world-space.
- `bg.outstanding()`: submitted jobs that have not finished executing.
- `bg.buffer_bytes()`: live native buffer payload, including buffers retained by
  Lua/results/render batches.

There are limits of 64 outstanding jobs and 16 MiB of live buffer payload per
application. Exceeding a limit raises an error; requests are never silently
dropped. Mutable builders are usable only by their creating thread.

Native handles wrap the same C++ allocations in both contexts; bulk payloads are
not copied through `SharedObject` or reconstructed as main-thread Lua tables.
Small control values are copied, and CPU-to-GPU uploads still copy bytes.

Tasks can use math, noise, vectors, logging, `ax` math helpers and registered
modules via `ax.import`. Script/module sources are snapshotted at startup and
cached in the task context. IO/OS facilities remain permission-gated. Tasks cannot
access window, camera, sprites, input/events, deferred Lua callbacks, dynamic
file/module loading or main-context Lua objects. Do not submit a script that
depends on those bindings. Tasks share the existing single worker thread with
legacy `bg.run_script` work, so a long legacy job delays new jobs. Existing
`bg.run_script` and `SharedObject` APIs remain available.

### Retained sprite batches and groups

On the main thread, attach a published sprite buffer:

```lua
local batch = app.sprites.attach_batch("tilesheet", result.sprites, chunk_order)
-- Attachment starts hidden and ineligible for GPU staging.
-- Request staging before ready(), independently of draw visibility.
batch:set_staging(true, math.floor(distance_squared))
if batch:ready() then batch:set_visible(true) end
-- When no longer needed onscreen, withdraw any pending upload:
batch:set_staging(false, math.floor(distance_squared))
batch:set_visible(false)
-- At scene exit:
batch:release()
```

Each batch accepts 1-1,024 sealed records, uploads once, and retains its storage
buffer. Hidden batches do not draw. GPU staging is opt-in via
`set_staging(eligible, priority)`, independently of `set_visible(bool)`, so a
hidden batch can upload before becoming ready to draw. Unrequested batches retain
only their CPU payload and do not upload. Tower Mancer requests staging only for
chunks that want to become visible, using squared camera distance as priority,
and withdraws requests for offscreen or retiring chunks.

At most one batch (64 KiB) uploads per frame, in subsequent frames after the
request. Lower integer staging priorities upload first; equal priorities use
`chunk_order`, then attachment order. Requests can be withdrawn or reprioritized
until upload. Hiding a batch or withdrawing staging does not evict an existing GPU
buffer; it stays resident until `release()`. No additional upload occurs when the
batch becomes visible again.

`ready()`, `error()`, `visible()`, `staging_eligible()`, `staging_priority()` and
`size()` expose its state. Check `error()` if staging fails; failed batches are
not retried. `release()` is idempotent; subsequent visibility or staging changes
are invalid.

The required integer `chunk_order` defines static-batch ordering, independent of
task completion. Lower orders draw first; static batches draw before ordinary
sprites in both the opaque and transparency passes. Use distinct orders where
equal-depth overlap matters. Atlas offsets are applied during GPU staging without
altering the shared CPU allocation.

For mutable sprites, `app.sprites.group()` creates a hidden group.
`app.sprites.allocate(group)` uses the usual sprite properties/setup/update API.
`group:set_visible(bool)` toggles the whole group without freeing its sprites;
`app.sprites.transfer(sprite, group)` transfers membership without replacing the
sprite. `group:release()` frees its owned sprites and is idempotent.
Sprite handles reject direct access after freeing/release, even if their old slot
is reused. Do not retain references to sprite vector properties past sprite
lifetime. These renderer APIs belong on the main thread.

`app.sprites.stats()` reports retained dynamic `sprites`, cumulative `allocations`
and `frees`, live `static_batches`, and cumulative `static_uploads`.
`app.clock()` is a monotonic clock in seconds. `app.sprites.setup_deadline()`
provides the current frame's soft setup deadline.
