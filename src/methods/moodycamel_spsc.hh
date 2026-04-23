#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <readerwriterqueue/readerwriterqueue.h>

// moodycamel::ReaderWriterQueue — lock-free SPSC queue. Producer enqueues the send
// timestamp, consumer busy-spins on try_dequeue.
struct moodycamel_spsc : ILatencyMethod
{
    explicit moodycamel_spsc(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#80deea"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        moodycamel::ReaderWriterQueue<std::uint64_t> q(1024);
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
