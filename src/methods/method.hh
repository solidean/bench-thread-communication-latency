#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct ILatencyMethod
{
    ILatencyMethod(std::string name, std::string display_name = {})
        : _name(std::move(name))
        , _display_name(display_name.empty() ? _name : std::move(display_name))
    {}

    virtual ~ILatencyMethod() = default;

    std::string const& name() const { return _name; }
    std::string const& display_name() const { return _display_name; }

    // Run num_samples round-trip iterations; returns the measured one-way forward-edge
    // latencies (ns). Producer = caller thread. The consumer thread is spawned internally.
    virtual std::vector<std::uint64_t> run(std::size_t num_samples) = 0;

    // Hex color string (e.g. "#6830FF"). Written to CSV and used by create-charts.py.
    virtual std::string color() const = 0;

private:
    std::string _name;
    std::string _display_name;
};
