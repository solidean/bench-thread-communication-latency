#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <boost/lockfree/spsc_queue.hpp>

// boost::lockfree::spsc_queue — wait-free SPSC ring buffer from Boost.Lockfree.
struct boost_lockfree_spsc : ILatencyMethod
{
    explicit boost_lockfree_spsc(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#7e57c2"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        boost::lockfree::spsc_queue<std::uint64_t, boost::lockfree::capacity<1024>> q;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                std::uint64_t t_send;
                while (true)
                {
                    while (!q.pop(t_send))
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

            while (!q.push(now_ns()))
            { /* full — shouldn't happen in steady state */
            }
        }

        consumer.join();
        return latencies;
    }
};
