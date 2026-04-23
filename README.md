# bench-thread-communication-latency

Microbenchmark for one-way thread-to-thread wakeup latency in C++.

For each method, the main thread (producer) captures a `steady_clock` timestamp (nanoseconds, `uint64_t`) and publishes it to a worker thread (consumer) via the method under test. The consumer immediately captures its own `steady_clock` timestamp on wakeup, appends `t_recv − t_send` to a preallocated `std::vector<uint64_t>`, and signals back via a plain spinning atomic (this back-edge is *not* measured, only the forward edge is). The loop repeats until the vector has 10 000 samples. The full per-sample distribution per method is dumped to `result.csv` and rendered as a log-scale ridgeplot (`chart_latency.svg`) with one histogram row per method, x-axis from 1 ns to 10 s.

A short warm-up (200 samples, discarded) runs before each timed run to amortize thread spawn and first-touch faults.

## Structure

```
src/
├── helper.hh                       — steady_clock now_ns() + timer
├── main.cc                         — driver, CSV writer, percentile summary
└── methods/
    ├── method.hh                   — ILatencyMethod base (name + run(num_samples) + color)
    ├── mutex_condvar.hh            — std::mutex + std::condition_variable
    ├── atomic_wait_notify.hh       — std::atomic<uint64_t>::wait / notify_one
    ├── spin_atomic.hh              — busy-spin on std::atomic counter
    ├── std_barrier.hh              — 2-party std::barrier
    └── sleep_poll_1ms.hh           — consumer sleeps 1 ms between atomic polls
create-charts.py                    — seaborn ridgeplot (log x, 1 ns..10 s)
build.py / run.py / run-debug.py    — thin CMake wrappers; build.py auto-wires vcpkg if present
vcpkg.json                          — empty manifest (no current deps, kept for future)
CMakeLists.txt                      — requires only Threads
```

Adding a new method: drop a header into `src/methods/` that inherits `ILatencyMethod`, implement `run(num_samples)` to spawn its own consumer thread and return the latency vector, then register a `std::make_unique` of it in `main.cc`.

## Methods

| name                    | mechanism                                                                                               |
| ----------------------- | ------------------------------------------------------------------------------------------------------- |
| `spin_atomic`           | Consumer busy-spins on `std::atomic<uint64_t>`. Floor for cross-core wakeup latency; no kernel involvement. |
| `atomic_wait_notify`    | `std::atomic<uint64_t>::wait` / `notify_one` (C++20). Futex / WaitOnAddress / __ulock on the kernel side. |
| `std_barrier`           | 2-party `std::barrier`; consumer already parked, producer's `arrive_and_wait` releases both.           |
| `mutex_condvar`         | Producer locks, writes ts, releases, `cv.notify_one()`; consumer blocks in `cv.wait()`.                |
| `sleep_poll_1ms`        | Consumer polls an atomic between `sleep_for(1ms)` calls — the "just sleep and check" baseline.        |

## Requirements

- C++23 compiler (MSVC 19.44+, GCC 14+, Clang 18+)
- CMake ≥ 3.25
- Python 3.11+ with [uv](https://docs.astral.sh/uv/) for the helper scripts (dependencies are pinned inline via PEP 723)

A vcpkg submodule is present but the current manifest has no dependencies — vcpkg is only invoked if something is added to `vcpkg.json`.

## Usage

```bash
uv run build.py         # configure + build (release)
uv run run.py           # build + run + create-charts; writes result.csv + chart_latency.svg
uv run create-charts.py # reads result.csv, writes chart_latency.svg
uv run run-debug.py     # debug build + run variant (does not create charts)
```

## TODO — more methods to explore

- **`std::binary_semaphore` / `std::counting_semaphore`** — release/acquire pair, likely similar cost profile to `atomic_wait_notify` but worth a separate row.
- **`std::latch`, one per sample** — measures latch construction cost alongside wakeup (single-shot).
- **Lock-free SPSC ring buffer** — single-slot `std::atomic<uint64_t>` seq, no OS call on either side; comparison point for `spin_atomic`.
- **Raw `pthread_cond_t` / `pthread_mutex_t`** — vs the C++ wrapper, to see if `libstdc++` adds any overhead.
- **Direct futex (Linux) / `WaitOnAddress` + `WakeByAddressSingle` (Windows) / `__ulock_wait` (macOS)** — the kernel primitive underneath `atomic::wait`, without the std wrapper.
- **Pipe / `socketpair` write-then-read** — kernel round-trip baseline (should land near `mutex_condvar` or worse).
- **Windows `SetEvent` / `WaitForSingleObject`** — classic Win32 event, platform counterpart to condvar.
- **Windows I/O completion port post/dequeue** — pattern used by `ASIO` and the Windows thread pool.
- **`moodycamel::ConcurrentQueue` / `boost::lockfree::spsc_queue`** — external MPMC/SPSC lock-free queues.
- **Hybrid spin-then-block** — spin for N ns (e.g. 1 µs), fall back to cv/atomic-wait. Should combine the low-end tail of spin with the CPU friendliness of block.
- **`SwitchToThread` / `Sleep(0)` / `yield` polling** — variants of the sleep-poll family, no fixed sleep.
- **`sleep_poll_1ms` + `timeBeginPeriod(1)`** — Windows-only; should shave off the default timer granularity.
- **Thread affinity variants** — pin producer+consumer to the same physical core, same SMT pair, different cores on same socket, different sockets. Latency floor changes by ~order of magnitude across these.
- **Cache-line alignment / padding study** — currently the forward-edge atomics share cache-line space with the back-edge `consumer_ready` flag; separate them and quantify the impact.
