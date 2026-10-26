## Fork-join jobs

`ax::JobSystem` ([job_system.h](../src/axeng/core/job_system.h)) is a fixed pool of worker threads for work that
is split up and joined within one call, such as one step of a simulation. It is native-only; Lua tasks use
`bg.submit` (see [lua threading.md](lua%20threading.md)).

```cpp
ax::JobSystem jobs{};                     // hardware threads less two
ax::JobSystem::Group group{ jobs };
for (auto& part : parts) group.run([&part] { part.update(); });
group.wait();                             // rethrows the first exception

jobs.parallel_for(items.size(), 256, [&](std::size_t begin, std::size_t end)
{
    for (auto i{ begin }; i < end; ++i) process(items[i]);
});
```

- A thread that waits on a group, or runs `parallel_for`, executes queued tasks until its own work is done. Tasks
  may therefore fork and wait for nested work without deadlocking the pool.
- `parallel_for` cuts `[0, count)` into consecutive chunks of `grain` indices. The chunks depend only on `count`
  and `grain`, never on the number of workers, so per-chunk results can be merged in chunk order deterministically.
- With zero workers every task runs on the calling thread, which keeps single-threaded runs and tests simple.
- Workers are named `Job worker N` in Tracy captures.
- Tasks run in no particular order. Shared state written by more than one task needs its own synchronisation, or
  per-task output merged after the wait.
