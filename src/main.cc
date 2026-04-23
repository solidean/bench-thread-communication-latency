#include "helper.hh"
#include "methods/atomic_wait_notify.hh"
#include "methods/binary_semaphore.hh"
#include "methods/boost_lockfree_spsc.hh"
#include "methods/hybrid_spin_block.hh"
#include "methods/latch_per_sample.hh"
#include "methods/method.hh"
#include "methods/moodycamel_mpmc.hh"
#include "methods/moodycamel_spsc.hh"
#include "methods/mutex_condvar.hh"
#include "methods/pthread_cond.hh"
#include "methods/sleep_poll_1ms.hh"
#include "methods/spin_atomic.hh"
#include "methods/spin_atomic_pause.hh"
#include "methods/spin_atomic_timestamp.hh"
#include "methods/spsc_ring.hh"
#include "methods/std_barrier.hh"
#include "methods/yield_poll.hh"

#if defined(__linux__)
#include "methods/futex_direct.hh"
#endif

#if !defined(_WIN32)
#include "methods/pipe_rw.hh"
#include "methods/socketpair_rw.hh"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <print>
#include <vector>

constexpr std::size_t NUM_SAMPLES = 20'000;
constexpr std::size_t WARMUP_SAMPLES = 200;

int main()
{
    timer total_timer;

    std::vector<std::unique_ptr<ILatencyMethod>> methods;
    methods.emplace_back(std::make_unique<spin_atomic>("spin_atomic"));
    methods.emplace_back(std::make_unique<spin_atomic_pause>("spin_atomic_pause"));
    methods.emplace_back(std::make_unique<spin_atomic_timestamp>("spin_atomic_timestamp"));
    methods.emplace_back(std::make_unique<spsc_ring>("spsc_ring"));
    methods.emplace_back(std::make_unique<moodycamel_spsc>("moodycamel_spsc"));
    methods.emplace_back(std::make_unique<boost_lockfree_spsc>("boost_lockfree_spsc"));
    methods.emplace_back(std::make_unique<moodycamel_mpmc>("moodycamel_mpmc"));
    methods.emplace_back(std::make_unique<yield_poll>("yield_poll"));
    methods.emplace_back(std::make_unique<hybrid_spin_block>("hybrid_spin_block"));
#if defined(__linux__)
    methods.emplace_back(std::make_unique<futex_direct>("futex_direct"));
#endif
    methods.emplace_back(std::make_unique<atomic_wait_notify>("atomic_wait_notify"));
    methods.emplace_back(std::make_unique<binary_semaphore>("binary_semaphore"));
    methods.emplace_back(std::make_unique<std_barrier>("std_barrier"));
    methods.emplace_back(std::make_unique<pthread_cond>("pthread_cond"));
    methods.emplace_back(std::make_unique<mutex_condvar>("mutex_condvar"));
    methods.emplace_back(std::make_unique<latch_per_sample>("latch_per_sample"));
#if !defined(_WIN32)
    methods.emplace_back(std::make_unique<pipe_rw>("pipe_rw"));
    methods.emplace_back(std::make_unique<socketpair_rw>("socketpair_rw"));
#endif
    // quite slow in comparison
    // methods.emplace_back(std::make_unique<sleep_poll_1ms>("sleep_poll_1ms"));

    struct method_result
    {
        std::string name;
        std::string color;
        std::vector<std::uint64_t> samples;
        std::uint64_t median;
    };
    std::vector<method_result> results;
    results.reserve(methods.size());

    for (auto const& m : methods)
    {
        std::println("=== {} ===", m->display_name());

        // warm-up pass (discarded): amortizes thread spawn + first-touch faults
        (void)m->run(WARMUP_SAMPLES);

        timer t;
        auto samples = m->run(NUM_SAMPLES);
        auto const secs = t.elapsed_secs();

        // sort a copy for percentile stats (keep sample order unchanged)
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

        results.push_back({std::string(m->name()), std::string(m->color()), std::move(samples), pct(0.50)});
        std::fflush(stdout);
    }

    std::ranges::sort(results, {}, &method_result::median);

    std::ofstream csv("result.csv");
    csv << "method,latency_ns,color\n";
    for (auto const& r : results)
        for (auto ns : r.samples)
            csv << r.name << "," << ns << "," << r.color << "\n";
    csv.close();

    auto const csv_path = std::filesystem::absolute("result.csv");
    std::println("");
    std::println("result written to: {}", csv_path.string());
    std::println("total time: {:.2f} s", total_timer.elapsed_secs());
}
