#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

// Consumer busy-spins on a std::atomic<uint64_t> sequence counter. Main thread stores the
// timestamp and increments the counter. No kernel involvement on the forward edge — this is
// the floor for cross-core wakeup latency.
struct spin_atomic : ILatencyMethod
{
    explicit spin_atomic(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#ffe66d"; }

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
                    { /* busy spin */
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
            }
            consumer_ready.store(false, std::memory_order_relaxed);

            t_send_storage = now_ns();
            seq.fetch_add(1, std::memory_order_release);
        }

        consumer.join();
        return latencies;
    }
};
