#include "helper.hh"
#include "methods/atomic_wait_notify.hh"
#include "methods/method.hh"
#include "methods/mutex_condvar.hh"
#include "methods/sleep_poll_1ms.hh"
#include "methods/spin_atomic.hh"
#include "methods/std_barrier.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <print>
#include <vector>

constexpr std::size_t NUM_SAMPLES = 10'000;
constexpr std::size_t WARMUP_SAMPLES = 200;

int main()
{
    timer total_timer;

    std::vector<std::unique_ptr<ILatencyMethod>> methods;
    methods.emplace_back(std::make_unique<spin_atomic>("spin_atomic"));
    methods.emplace_back(std::make_unique<atomic_wait_notify>("atomic_wait_notify"));
    methods.emplace_back(std::make_unique<std_barrier>("std_barrier"));
    methods.emplace_back(std::make_unique<mutex_condvar>("mutex_condvar"));
    methods.emplace_back(std::make_unique<sleep_poll_1ms>("sleep_poll_1ms"));

    std::ofstream csv("result.csv");
    csv << "method,latency_ns,color\n";

    for (auto const& m : methods)
    {
        std::println("=== {} ===", m->display_name());

        // warm-up pass (discarded): amortizes thread spawn + first-touch faults
        (void)m->run(WARMUP_SAMPLES);

        timer t;
        auto const samples = m->run(NUM_SAMPLES);
        auto const secs = t.elapsed_secs();

        // sort a copy for percentile stats (keep CSV order unchanged)
        auto sorted = samples;
        std::ranges::sort(sorted);
        auto const pct = [&](double p)
        { return sorted[std::min<std::size_t>(sorted.size() - 1, std::size_t(sorted.size() * p))]; };

        std::println("  {} samples in {:.3f} s", samples.size(), secs);
        std::println("  min      {:>10} ns", sorted.front());
        std::println("  p50      {:>10} ns", pct(0.50));
        std::println("  p90      {:>10} ns", pct(0.90));
        std::println("  p99      {:>10} ns", pct(0.99));
        std::println("  max      {:>10} ns", sorted.back());

        for (auto ns : samples)
            csv << m->name() << "," << ns << "," << m->color() << "\n";
        std::fflush(stdout);
    }

    csv.close();

    auto const csv_path = std::filesystem::absolute("result.csv");
    std::println("");
    std::println("result written to: {}", csv_path.string());
    std::println("total time: {:.2f} s", total_timer.elapsed_secs());
}
