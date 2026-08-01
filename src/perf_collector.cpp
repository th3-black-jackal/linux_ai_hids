#include "hids/perf_collector.hpp"

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace hids {

static long perf_event_open_(perf_event_attr* attr, pid_t pid, int cpu,
                             int group_fd, unsigned long flags) {
    return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

std::uint64_t PerfCollector::tracepointId(const std::string& tracefsPath) {
    static const char* kBases[] = {
        "/sys/kernel/tracing/events/",
        "/sys/kernel/debug/tracing/events/",
    };
    for (const char* base : kBases) {
        std::string p = std::string(base) + tracefsPath + "/id";
        if (FILE* f = std::fopen(p.c_str(), "r")) {
            std::uint64_t id = 0;
            int got = std::fscanf(f, "%" SCNu64, &id);
            std::fclose(f);
            if (got == 1) return id;
        }
    }
    return 0;  // not found on this kernel
}

std::uint64_t PerfCollector::readGroup(const std::vector<int>& fds) {
    std::uint64_t total = 0, v = 0;
    for (int fd : fds) {
        if (fd >= 0 && ::read(fd, &v, sizeof(v)) == static_cast<ssize_t>(sizeof(v)))
            total += v;
    }
    return total;
}

PerfCollector::PerfCollector(
    const std::vector<std::pair<std::string, std::string>>& routes) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    ncpu_ = (n > 0) ? static_cast<int>(n) : 1;

    for (const auto& [feature, path] : routes) {
        Counter c;
        c.feature = feature;

        std::uint64_t id = tracepointId(path);
        if (id == 0) {
            missing_.push_back(feature);
            counters_.push_back(std::move(c));   // stays 0 forever
            continue;
        }

        perf_event_attr attr{};
        attr.size   = sizeof(attr);
        attr.type   = PERF_TYPE_TRACEPOINT;
        attr.config = id;
        // count system-wide: pid=-1 requires a specific cpu, so open per-CPU.
        for (int cpu = 0; cpu < ncpu_; ++cpu) {
            int fd = static_cast<int>(perf_event_open_(&attr, -1, cpu, -1, 0));
            if (fd >= 0) c.fds.push_back(fd);
        }
        if (c.fds.empty()) missing_.push_back(feature);
        counters_.push_back(std::move(c));
    }
}

PerfCollector::~PerfCollector() {
    for (auto& c : counters_)
        for (int fd : c.fds)
            if (fd >= 0) ::close(fd);
}

void PerfCollector::sample(std::map<std::string, double>& out) {
    for (auto& c : counters_) {
        std::uint64_t cur = readGroup(c.fds);
        std::uint64_t delta = (cur >= c.prev) ? (cur - c.prev) : cur;  // handle wrap/reset
        c.prev = cur;
        out[c.feature] = static_cast<double>(delta);
    }
}

}  // namespace hids
