#pragma once

#include "../helper.hh"
#include "method.hh"

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <thread>
#include <utility>
#include <vector>

// Raw pthread_mutex_t + pthread_cond_t — mirrors mutex_condvar to reveal any overhead
// added by the libstdc++ std::mutex / std::condition_variable wrappers.
struct pthread_cond : ILatencyMethod
{
    explicit pthread_cond(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#e57373"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
        std::uint64_t shared_ts = 0;
        bool has_ts = false;
        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                while (true)
                {
                    pthread_mutex_lock(&mtx);
                    while (!has_ts)
                        pthread_cond_wait(&cv, &mtx);
                    auto const t_recv = now_ns();
                    auto const t_send = shared_ts;
                    has_ts = false;
                    pthread_mutex_unlock(&mtx);

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

            auto const t_send = now_ns();
            pthread_mutex_lock(&mtx);
            shared_ts = t_send;
            has_ts = true;
            pthread_mutex_unlock(&mtx);
            pthread_cond_signal(&cv);
        }

        consumer.join();
        pthread_cond_destroy(&cv);
        pthread_mutex_destroy(&mtx);
        return latencies;
    }
};
