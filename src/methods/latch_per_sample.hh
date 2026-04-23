#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <deque>
#include <latch>
#include <thread>
#include <utility>
#include <vector>

// Single-shot std::latch, constructed fresh every sample. Producer pushes a new latch
// plus the send timestamp; consumer pops the slot, wait()s, records the latency. All
// latches live in a deque for the entire run so their storage is stable — std::latch
// destruction while another thread is still inside wait() would be UB.
struct latch_per_sample : ILatencyMethod
{
    explicit latch_per_sample(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#d4e157"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        struct slot_t
        {
            std::latch latch{1};
            std::uint64_t t_send = 0;
        };
        std::deque<slot_t> slots; // stable addresses
        std::atomic<slot_t*> pending{nullptr};
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                while (true)
                {
                    slot_t* s;
                    while ((s = pending.exchange(nullptr, std::memory_order_acquire)) == nullptr)
                    { /* spin — producer will publish soon */
                    }
                    s->latch.wait();
                    auto const t_recv = now_ns();
                    auto const t_send = s->t_send;

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

            auto& s = slots.emplace_back();
            pending.store(&s, std::memory_order_release);

            s.t_send = now_ns();
            s.latch.count_down();
        }

        consumer.join();
        return latencies;
    }
};
