#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define SPIN_ATOMIC_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define SPIN_ATOMIC_PAUSE() asm volatile("yield" ::: "memory")
#else
#define SPIN_ATOMIC_PAUSE() ((void)0)
#endif

// Like spin_atomic, but the busy-spin loops issue a pause/yield hint on each iteration.
// Reduces pipeline pressure and power use vs a raw spin, at the cost of a small wakeup delay.
struct spin_atomic_pause : ILatencyMethod
{
    explicit spin_atomic_pause(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#ffd93d"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        std::atomic<std::uint64_t> seq{0};
        std::uint64_t t_send_storage = 0;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                std::uint64_t last = 0;
                while (true)
                {
                    while (seq.load(std::memory_order_acquire) == last)
                    {
                        SPIN_ATOMIC_PAUSE();
                    }
                    auto const t_recv = now_ns();
                    last = seq.load(std::memory_order_relaxed);
                    auto const t_send = t_send_storage;

                    latencies.push_back(t_recv - t_send);
                    if (latencies.size() >= num_samples)
                        return;

                    consumer_ready.store(true, std::memory_order_release);
                }
            });

        for (std::size_t i = 0; i < num_samples; ++i)
        {
            while (!consumer_ready.load(std::memory_order_acquire))
            {
                SPIN_ATOMIC_PAUSE();
            }
            consumer_ready.store(false, std::memory_order_relaxed);

            t_send_storage = now_ns();
            seq.fetch_add(1, std::memory_order_release);
        }

        consumer.join();
        return latencies;
    }
};

#undef SPIN_ATOMIC_PAUSE
