// proc_collector.hpp -- derives the I/O features (util, write_kbs, write_merge,
// iowrite, iowritetime) from /proc/diskstats deltas over each window.
//
// Sums across whole-disk devices only (partitions filtered via /sys/block), so
// it works whether the target names its disk mmcblk0 (Pi) or vda (qemu virt).
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "hids/collector.hpp"

namespace hids {

class ProcCollector : public ICollector {
public:
    // Which feature names this collector is responsible for is fixed; the flags
    // let the service omit any the model doesn't use.
    explicit ProcCollector();

    void sample(std::map<std::string, double>& out) override;
    const char* name() const override { return "proc"; }

private:
    struct Totals {
        std::uint64_t writes_merged = 0;   // diskstats field 6
        std::uint64_t sectors_written = 0; // field 7 (512-byte sectors)
        std::uint64_t ms_writing = 0;      // field 8
        std::uint64_t io_ticks = 0;        // field 10 (ms the device was busy)
    };

    static bool isWholeDisk(const std::string& dev);
    Totals readDiskstats() const;

    Totals prev_{};
    bool    have_prev_ = false;
    double  prev_time_ = 0.0;   // steady seconds
};

}  // namespace hids
