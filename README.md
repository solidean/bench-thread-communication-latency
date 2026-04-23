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
    ├── spin_atomic.hh              — busy-spin on std::atomic counter
    ├── spsc_ring.hh                — hand-rolled lock-free SPSC ring buffer
    ├── moodycamel_spsc.hh          — moodycamel::ReaderWriterQueue (vcpkg)
    ├── moodycamel_mpmc.hh          — moodycamel::ConcurrentQueue (vcpkg)
    ├── boost_lockfree_spsc.hh      — boost::lockfree::spsc_queue (vcpkg)
    ├── yield_poll.hh               — poll atomic with std::this_thread::yield()
    ├── hybrid_spin_block.hh        — spin 1 µs, fall back to atomic::wait
    ├── futex_direct.hh             — raw Linux SYS_futex (FUTEX_WAIT/WAKE_PRIVATE)
    ├── atomic_wait_notify.hh       — std::atomic<uint64_t>::wait / notify_one
    ├── binary_semaphore.hh         — std::binary_semaphore release/acquire
    ├── std_barrier.hh              — 2-party std::barrier
    ├── pthread_cond.hh             — raw pthread_mutex_t + pthread_cond_t
    ├── mutex_condvar.hh            — std::mutex + std::condition_variable
    ├── latch_per_sample.hh         — fresh std::latch(1) per sample
    ├── pipe_rw.hh                  — POSIX anonymous pipe write/read
    ├── socketpair_rw.hh            — AF_UNIX socketpair write/read
    └── sleep_poll_1ms.hh           — consumer sleeps 1 ms between atomic polls
create-charts.py                    — seaborn ridgeplot (log x, 1 ns..~3 ms)
build.py / run.py / run-debug.py    — thin CMake wrappers; build.py auto-wires vcpkg if present
vcpkg.json                          — readerwriterqueue, concurrentqueue, boost-lockfree
CMakeLists.txt                      — Threads + the three vcpkg queue libs
```

Adding a new method: drop a header into `src/methods/` that inherits `ILatencyMethod`, implement `run(num_samples)` to spawn its own consumer thread and return the latency vector, then register a `std::make_unique` of it in `main.cc`.

## Methods

Rows are ordered roughly fastest-expected to slowest-expected; this matches the CSV order used for the ridgeplot.

| name                    | mechanism                                                                                               |
| ----------------------- | ------------------------------------------------------------------------------------------------------- |
| `spin_atomic`           | Consumer busy-spins on `std::atomic<uint64_t>`. Floor for cross-core wakeup latency; no kernel involvement. |
| `spsc_ring`             | Hand-rolled lock-free SPSC ring buffer (64 slots, separate head/tail seqs).                            |
| `moodycamel_spsc`       | [`moodycamel::ReaderWriterQueue`](https://github.com/cameron314/readerwriterqueue) — lock-free SPSC queue. |
| `boost_lockfree_spsc`   | `boost::lockfree::spsc_queue` — wait-free SPSC ring buffer from Boost.Lockfree.                        |
| `moodycamel_mpmc`       | [`moodycamel::ConcurrentQueue`](https://github.com/cameron314/concurrentqueue) used as SPSC — popular general-purpose lock-free MPMC. |
| `yield_poll`            | Consumer polls the seq atomic, calls `std::this_thread::yield()` between polls.                        |
| `hybrid_spin_block`     | Consumer spins ~1 µs, then falls back to `atomic::wait`. Producer always `notify_one`s.                |
| `futex_direct`          | Raw Linux `SYS_futex` (`FUTEX_WAIT_PRIVATE` / `FUTEX_WAKE_PRIVATE`), no std wrapper. Linux only.       |
| `atomic_wait_notify`    | `std::atomic<uint64_t>::wait` / `notify_one` (C++20). Futex / WaitOnAddress / __ulock on the kernel side. |
| `binary_semaphore`      | `std::binary_semaphore` release/acquire. Typically the same primitive as `atomic::wait` with a thinner API. |
| `std_barrier`           | 2-party `std::barrier`; consumer already parked, producer's `arrive_and_wait` releases both.           |
| `pthread_cond`          | Raw `pthread_mutex_t` + `pthread_cond_t`. Same shape as `mutex_condvar`, skips the libstdc++ wrapper.  |
| `mutex_condvar`         | Producer locks, writes ts, releases, `cv.notify_one()`; consumer blocks in `cv.wait()`.                |
| `latch_per_sample`      | Fresh `std::latch(1)` constructed every sample; measures latch construction + wakeup. Single-shot.     |
| `pipe_rw`               | POSIX anonymous pipe: producer `write`s 8 bytes, consumer blocks in `read`. Kernel round-trip.         |
| `socketpair_rw`         | `socketpair(AF_UNIX, SOCK_STREAM)` — same write/read pattern through the socket layer.                 |
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

- **`__ulock_wait` (macOS)** — macOS kernel primitive underneath `atomic::wait`, counterpart to the `futex_direct` row.
- **Windows `SetEvent` / `WaitForSingleObject`** — classic Win32 event, platform counterpart to condvar.
- **Windows I/O completion port post/dequeue** — pattern used by `ASIO` and the Windows thread pool.
- **`SwitchToThread` / `Sleep(0)` polling (Windows)** — Windows counterparts to `yield_poll`.
- **`sleep_poll_1ms` + `timeBeginPeriod(1)`** — Windows-only; should shave off the default timer granularity.
- **Thread affinity variants** — pin producer+consumer to the same physical core, same SMT pair, different cores on same socket, different sockets. Latency floor changes by ~order of magnitude across these.
- **Cache-line alignment / padding study** — currently the forward-edge atomics share cache-line space with the back-edge `consumer_ready` flag; separate them and quantify the impact.
