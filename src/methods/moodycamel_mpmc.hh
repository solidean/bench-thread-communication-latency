#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <concurrentqueue/moodycamel/concurrentqueue.h>

// moodycamel::ConcurrentQueue — lock-free MPMC queue used here in a single-producer,
// single-consumer configuration. Heavier than the SPSC-specialized queue but widely
// deployed as a general-purpose lock-free queue.
struct moodycamel_mpmc : ILatencyMethod
{
    explicit moodycamel_mpmc(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#26c6da"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        moodycamel::ConcurrentQueue<std::uint64_t> q;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                std::uint64_t t_send;
                while (true)
                {
                    while (!q.try_dequeue(t_send))
                    { /* spin */
                    }
                    auto const t_recv = now_ns();

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

            q.enqueue(now_ns());
        }

        consumer.join();
        return latencies;
    }
};
