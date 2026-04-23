#pragma once

#include "../helper.hh"
#include "method.hh"

#include <algorithm>
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

// Variant of spin_atomic where the sequence atomic doubles as the send timestamp: the producer
// stores now_ns() directly (forced monotonic via max(now_ns(), last+1)). This removes the
// separate t_send_storage write, so the consumer sees the send time in the same cacheline
// transfer as the signal. seq and consumer_ready live on their own cachelines.
struct spin_atomic_timestamp : ILatencyMethod
{
    explicit spin_atomic_timestamp(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#ffbf00"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        struct alignas(64) padded_u64
        {
            std::atomic<std::uint64_t> v{0};
        };
        struct alignas(64) padded_bool
        {
            std::atomic<bool> v{true};
        };

        padded_u64 seq;
        padded_bool consumer_ready;

        std::thread consumer(
            [&]
            {
                std::uint64_t last = 0;
                while (true)
                {
                    std::uint64_t t_send;
                    while ((t_send = seq.v.load(std::memory_order_acquire)) == last)
                    {
                        SPIN_ATOMIC_PAUSE();
                    }
                    auto const t_recv = now_ns();
                    last = t_send;

                    latencies.push_back(t_recv - t_send);
                    if (latencies.size() >= num_samples)
                        return;

                    consumer_ready.v.store(true, std::memory_order_release);
                }
            });

        std::uint64_t last_ns = 0;
        for (std::size_t i = 0; i < num_samples; ++i)
        {
            while (!consumer_ready.v.load(std::memory_order_acquire))
            {
                SPIN_ATOMIC_PAUSE();
            }
            consumer_ready.v.store(false, std::memory_order_relaxed);

            auto const ns = std::max(now_ns(), last_ns + 1);
            seq.v.store(ns, std::memory_order_release);
            last_ns = ns;
        }

        consumer.join();
        return latencies;
    }
};

#undef SPIN_ATOMIC_PAUSE
