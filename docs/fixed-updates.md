# Fixed updates and frame pacing

`Window::set_fixed_update_rate(rate)` opts a window into fixed updates. Configure it before
`run_loop()`. The default rate is zero, preserving variable updates for existing applications.
Positive rates up to 1000 Hz are supported. The loop uses a steady clock and event-aware waits.

An update callback may change the rate of a running loop (Lua `app.window.set_update_rate(hz)`, 1 to 1000), which
restarts the loop's timing. Presentation is independent: `app.window.set_vsync(enabled)` rebuilds the surface with or
without vsync, and `app.window.set_position(x, y)` / `set_size(width, height)` place the window.

For each due update, callbacks run in this order:

1. `get_update_event_handler()` / Lua `app.on_update`: input and commands.
2. On the final update of a catch-up batch, `get_prepare_frame_event_handler()` / Lua
   `app.window.on_prepare_frame`: frame preparation and UI commands, once per frame attempt.
3. `get_simulation_event_handler()`: authoritative native simulation.
4. `get_post_update_event_handler()` / Lua `app.on_post_update`: result delivery and dependent systems.

The update, simulation and post-update events receive the fixed timestep. Frame preparation and
render/UI events receive real elapsed time since the previous successfully rendered frame. Keep
these clocks distinct. Frame preparation happens before simulation so UI commands can affect the
current tick; actual drawing reads completed state. Frame preparation can run on a frame attempt
whose later surface acquisition fails; do not use its invocation count as a presented frame count.

After the batch, the pre-render, render and UI passes run once for the latest completed state.
No surface is acquired and no frame is submitted when no newer update exists. Failed acquisition
preserves eligibility for retry and reconfigures the surface. Both successful acquisition statuses
(optimal and suboptimal) are accepted. Deferred callbacks are serviced even on iterations without
a tick or frame.

At most six updates run in a batch. Excess whole-tick wall-time debt is discarded and recorded;
fractional remainder is preserved. Input/window events are serviced between catch-up ticks.
Under sustained overload, actual updates per real second fall rather than enlarging the timestep
or allowing an unbounded backlog. This is a single-threaded scheduler, so update and render work
share the same CPU time budget. It cannot guarantee the target rate when that budget is exceeded.

For fixed-update windows, minimization suspends both updates and rendering. Restoration starts a
fresh deadline without catching up minimized time. An application-level world pause should leave
application/UI updates running and disable only world simulation.

Cosmetic GPU particles compute once per rendered frame using accumulated completed-update time.
They do not drive authoritative gameplay. Their pause setting still freezes them independently.

`Window::loop_statistics()` / Lua `app.window.loop_stats()` expose cumulative active wall seconds,
completed application updates, submitted frames, last rendered update, additional catch-up ticks,
discarded seconds, failed surface acquisitions, and latest update/render durations in milliseconds.
Derive rates from differences of counters over differences of active wall seconds. World UPS must
be measured separately from the application's authoritative simulation tick counter. Counters
reset when `run_loop()` starts; active wall time excludes minimized intervals.
