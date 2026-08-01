#include "hids/proc_collector.hpp"

#include <sys/stat.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hids {

static constexpr std::uint64_t kSectorBytes = 512;

static double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

ProcCollector::ProcCollector() = default;

bool ProcCollector::isWholeDisk(const std::string& dev) {
    // Partitions live under /sys/block/<disk>/<part>, so only whole disks have a
    // directory directly under /sys/block. This filters sda1, mmcblk0p1, etc.
    struct stat st{};
    std::string p = "/sys/block/" + dev;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

ProcCollector::Totals ProcCollector::readDiskstats() const {
    Totals t{};
    std::ifstream f("/proc/diskstats");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::vector<std::string> tok;
        std::string w;
        while (ss >> w) tok.push_back(w);
        if (tok.size() < 14) continue;               // need through io_ticks

        const std::string& dev = tok[2];
        if (!isWholeDisk(dev)) continue;             // skip partitions/virtual

        // fields (1-based from "reads completed") start at tok[3]:
        //   writes merged = f6 -> tok[8]
        //   sectors written = f7 -> tok[9]
        //   ms writing = f8 -> tok[10]
        //   io_ticks = f10 -> tok[12]
        try {
            t.writes_merged   += std::stoull(tok[8]);
            t.sectors_written += std::stoull(tok[9]);
            t.ms_writing      += std::stoull(tok[10]);
            t.io_ticks        += std::stoull(tok[12]);
        } catch (...) {
            // malformed line -> ignore
        }
    }
    return t;
}

void ProcCollector::sample(std::map<std::string, double>& out) {
    Totals cur = readDiskstats();
    double now = steadySeconds();

    if (!have_prev_) {
        prev_ = cur;
        prev_time_ = now;
        have_prev_ = true;
        // first window has no delta baseline -> emit zeros
        out["write_merge"]  = 0.0;
        out["iowrite"]      = 0.0;
        out["write_kbs"]    = 0.0;
        out["iowritetime"]  = 0.0;
        out["util"]         = 0.0;
        return;
    }

    auto d = [](std::uint64_t a, std::uint64_t b) -> double {
        return static_cast<double>(a >= b ? a - b : a);
    };
    double dWritesMerged = d(cur.writes_merged,   prev_.writes_merged);
    double dSectors      = d(cur.sectors_written, prev_.sectors_written);
    double dMsWriting    = d(cur.ms_writing,      prev_.ms_writing);
    double dIoTicks      = d(cur.io_ticks,        prev_.io_ticks);
    double elapsed       = now - prev_time_;
    if (elapsed <= 0.0) elapsed = 1e-3;

    double bytesWritten  = dSectors * kSectorBytes;

    out["write_merge"]  = dWritesMerged;                       // merged write ops
    out["iowrite"]      = bytesWritten;                        // bytes written
    out["write_kbs"]    = (bytesWritten / 1024.0) / elapsed;   // KB/s
    out["iowritetime"]  = dMsWriting;                          // ms spent writing
    out["util"]         = dIoTicks / (elapsed * 1000.0);       // busy fraction [0..1]

    prev_ = cur;
    prev_time_ = now;
}

}  // namespace hids
