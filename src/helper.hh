#pragma once

#include <chrono>
#include <cstdint>

struct timer
{
    // according to cppreference, "[steady_clock] is most suitable for measuring intervals"
    using clock = std::chrono::steady_clock;

    clock::time_point start = clock::now();

    [[nodiscard]] double elapsed_secs() const { return std::chrono::duration<double>(clock::now() - start).count(); }
};

[[nodiscard]] inline std::uint64_t now_ns()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
}
