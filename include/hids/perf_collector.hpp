// perf_collector.hpp -- counts syscall and kernel/fs tracepoints system-wide
// via perf_event_open, one fd per CPU. Each sample() returns per-window deltas.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "hids/collector.hpp"

namespace hids {

class PerfCollector : public ICollector {
public:
    // routes: featureName -> tracefs path, e.g. {"connect","syscalls/sys_enter_connect"}
    explicit PerfCollector(const std::vector<std::pair<std::string, std::string>>& routes);
    ~PerfCollector() override;

    void sample(std::map<std::string, double>& out) override;
    const char* name() const override { return "perf"; }

    // Names whose tracepoint id could not be resolved on this kernel (read as 0).
    const std::vector<std::string>& missing() const { return missing_; }

private:
    struct Counter {
        std::string      feature;
        std::vector<int> fds;        // one per CPU
        std::uint64_t    prev = 0;
    };

    static std::uint64_t tracepointId(const std::string& tracefsPath);
    static std::uint64_t readGroup(const std::vector<int>& fds);

    std::vector<Counter>     counters_;
    std::vector<std::string> missing_;
    int                      ncpu_ = 1;
};

}  // namespace hids
