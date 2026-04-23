#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Main thread locks the mutex, writes the timestamp, sets a flag, releases, notifies
// the consumer via a condition variable. Consumer blocks on cv.wait() until the flag is set.
struct mutex_condvar : ILatencyMethod
{
    explicit mutex_condvar(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#ff6b6b"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        std::mutex mtx;
        std::condition_variable cv;
        std::uint64_t shared_ts = 0;
        bool has_ts = false;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                while (true)
                {
                    std::unique_lock<std::mutex> lk(mtx);
                    cv.wait(lk, [&] { return has_ts; });
                    auto const t_recv = now_ns();
                    auto const t_send = shared_ts;
                    has_ts = false;
                    lk.unlock();

                    latencies.push_back(t_recv - t_send);
                    if (latencies.size() >= num_samples)
                        return;

                    consumer_ready.store(true, std::memory_order_release);
                }
            });

        for (std::size_t i = 0; i < num_samples; ++i)
        {
            while (!consumer_ready.load(std::memory_order_acquire))
            { /* back-edge spin, not measured */
            }
            consumer_ready.store(false, std::memory_order_relaxed);

            auto const t_send = now_ns();
            {
                std::lock_guard<std::mutex> lk(mtx);
                shared_ts = t_send;
                has_ts = true;
            }
            cv.notify_one();
        }

        consumer.join();
        return latencies;
    }
};
