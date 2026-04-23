#pragma once

#include "../helper.hh"
#include "method.hh"

#if !defined(_WIN32)

#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

// Anonymous pipe: producer writes 8 bytes (the send timestamp) into the pipe, consumer
// blocks in read(). Classic POSIX kernel round-trip baseline.
struct pipe_rw : ILatencyMethod
{
    explicit pipe_rw(std::string name) : ILatencyMethod(std::move(name)) {}

    std::string color() const override { return "#8d6e63"; }

    std::vector<std::uint64_t> run(std::size_t num_samples) override
    {
        std::vector<std::uint64_t> latencies;
        latencies.reserve(num_samples);

        int fds[2];
        if (::pipe2(fds, O_CLOEXEC) != 0)
            return latencies;

        std::atomic<bool> consumer_ready{true};

        std::thread consumer(
            [&]
            {
                while (true)
                {
                    std::uint64_t t_send = 0;
                    auto n = ::read(fds[0], &t_send, sizeof(t_send));
                    auto const t_recv = now_ns();
                    (void)n;

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

            std::uint64_t t_send = now_ns();
            auto n = ::write(fds[1], &t_send, sizeof(t_send));
            (void)n;
        }

        consumer.join();
        ::close(fds[0]);
        ::close(fds[1]);
        return latencies;
    }
};

#endif // !_WIN32
