#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <barrier>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

// 2-party std::barrier. Both threads call arrive_and_wait(); the consumer is already parked at
// the barrier when the producer writes the timestamp and arrives. The barrier auto-resets,
// so a single instance handles all samples. The timestamp is published via the barrier's
// synchronizing edge.
struct std_barrier : ILatencyMethod
{
    explicit std_barrier(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#a8e6cf"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        std::barrier<> bar(2);
        std::uint64_t t_send_storage = 0;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                for (std::size_t i = 0; i < num_samples; ++i)
                {
                    bar.arrive_and_wait();
                    auto const t_recv = now_ns();
                    auto const t_send = t_send_storage;

                    latencies.push_back(t_recv - t_send);
                    if (i + 1 < num_samples)
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
            bar.arrive_and_wait();
        }

        consumer.join();
        return latencies;
    }
};
