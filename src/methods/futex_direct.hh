#pragma once

#include "../helper.hh"
#include "method.hh"

#if defined(__linux__)

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// Direct Linux futex syscall (FUTEX_WAIT_PRIVATE / FUTEX_WAKE_PRIVATE) on a plain
// uint32_t. Skips the std::atomic::wait wrapper — lower bound for kernel-assisted
// wakeup on Linux.
struct futex_direct : ILatencyMethod
{
    explicit futex_direct(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#ef5350"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        alignas(64) std::atomic<std::uint32_t> seq{0};
        std::uint64_t t_send_storage = 0;
        std::atomic<bool> consumer_ready{true};

        auto futex_wait = [](std::atomic<std::uint32_t>* addr, std::uint32_t expected)
        { ::syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(addr), FUTEX_WAIT_PRIVATE, expected, nullptr, nullptr, 0); };
        auto futex_wake = [](std::atomic<std::uint32_t>* addr)
        { ::syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(addr), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0); };

        std::thread consumer(
            [&]
            {
                std::uint32_t last = 0;
                while (true)
                {
                    while (seq.load(std::memory_order_acquire) == last)
                        futex_wait(&seq, last);

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
            futex_wake(&seq);
        }

        consumer.join();
        return latencies;
    }
};

#endif // __linux__
