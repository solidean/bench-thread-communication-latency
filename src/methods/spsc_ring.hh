#pragma once

#include "../helper.hh"
#include "method.hh"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

// Hand-rolled lock-free SPSC ring buffer. Producer writes the send timestamp into a slot,
// then publishes the new head index with release. Consumer spins on the head index, reads
// the slot, and bumps tail with release. No kernel involvement — comparison point for
// spin_atomic, isolating the extra indexing work over a single-slot seq counter.
struct spsc_ring : ILatencyMethod
{
    explicit spsc_ring(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#f9a825"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        constexpr std::size_t CAP = 64; // power of two
        std::array<std::uint64_t, CAP> slots{};
        alignas(64) std::atomic<std::uint64_t> head{0}; // producer-written
        alignas(64) std::atomic<std::uint64_t> tail{0}; // consumer-written
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                std::uint64_t t = 0;
                while (true)
                {
                    while (head.load(std::memory_order_acquire) == t)
                    { /* spin */
                    }
                    auto const t_recv = now_ns();
                    auto const t_send = slots[t & (CAP - 1)];
                    ++t;
                    tail.store(t, std::memory_order_release);

                    latencies.push_back(t_recv - t_send);
                    if (latencies.size() >= num_samples)
                        return;

                    consumer_ready.store(true, std::memory_order_release);
                }
            });

        std::uint64_t h = 0;
        for (std::size_t i = 0; i < num_samples; ++i)
        {
            while (!consumer_ready.load(std::memory_order_acquire))
            {
            }
            consumer_ready.store(false, std::memory_order_relaxed);

            slots[h & (CAP - 1)] = now_ns();
            ++h;
            head.store(h, std::memory_order_release);
        }

        consumer.join();
        return latencies;
    }
};
