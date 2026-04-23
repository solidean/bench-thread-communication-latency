#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

// std::binary_semaphore release/acquire. Producer writes the timestamp into a side
// variable and calls release(); consumer blocks in acquire(). Typically maps to the same
// futex/WaitOnAddress primitive as atomic::wait, with a thinner API surface.
struct binary_semaphore : ILatencyMethod
{
    explicit binary_semaphore(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#ba68c8"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        std::binary_semaphore sem{0};
        std::uint64_t t_send_storage = 0; // published via sem release/acquire pair
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                while (true)
                {
                    sem.acquire();
                    auto const t_recv = now_ns();
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
            sem.release();
        }

        consumer.join();
        return latencies;
    }
};
