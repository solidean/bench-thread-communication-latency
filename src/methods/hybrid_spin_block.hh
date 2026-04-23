#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

// Consumer spins on the seq atomic for a short budget (~1 µs), then falls back to
// atomic::wait. Producer always does fetch_add + notify_one. Aims to combine the
// sub-microsecond median of spin_atomic with the CPU friendliness of atomic_wait_notify.
struct hybrid_spin_block : ILatencyMethod
{
    explicit hybrid_spin_block(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#66bb6a"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        constexpr std::uint64_t SPIN_NS = 1'000; // 1 µs

        std::atomic<std::uint64_t> seq{0};
        std::uint64_t t_send_storage = 0;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                std::uint64_t last = 0;
                while (true)
                {
                    // spin phase
                    auto const spin_start = now_ns();
                    while (seq.load(std::memory_order_acquire) == last)
                    {
                        if (now_ns() - spin_start >= SPIN_NS)
                        {
                            // block phase
                            seq.wait(last, std::memory_order_acquire);
                            break;
                        }
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
            seq.notify_one();
        }

        consumer.join();
        return latencies;
    }
};
