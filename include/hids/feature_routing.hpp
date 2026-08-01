// feature_routing.hpp -- decides, for each name in FEATURE_ORDER, which
// collector produces it and (for perf) the tracefs event path.
//
// Grouping for the released 31-feature set:
//   * contains ':'  -> perf tracepoint  (strip _KERN_data/_RES_data suffix,
//                                         replace ':' with '/')
//   * disk set      -> /proc/diskstats-derived  (util,write_kbs,write_merge,
//                                                 iowrite,iowritetime)
//   * resource set  -> seconds_RES_data
//   * otherwise     -> syscall tracepoint  (syscalls/sys_enter_<name>)
#pragma once

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hids {

struct FeatureRoute {
    // featureName -> tracefs path ("syscalls/sys_enter_connect", "block/block_unplug")
    std::vector<std::pair<std::string, std::string>> perf;
    std::vector<std::string> disk;        // diskstats-derived
    std::vector<std::string> resource;    // seconds_RES_data
};

inline std::string stripSuffix(const std::string& n) {
    static const char* kSuffixes[] = {"_KERN_data", "_RES_data"};
    for (const char* suf : kSuffixes) {
        std::string s(suf);
        if (n.size() > s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0)
            return n.substr(0, n.size() - s.size());
    }
    return n;
}

inline std::string tracefsPath(const std::string& name) {
    std::string base = stripSuffix(name);
    std::replace(base.begin(), base.end(), ':', '/');   // subsys:event -> subsys/event
    return base;
}

inline FeatureRoute routeFeatures(const std::vector<std::string>& order) {
    static const std::set<std::string> kDisk = {
        "util", "write_kbs", "write_merge", "iowrite", "iowritetime"};
    static const std::set<std::string> kResource = {"seconds_RES_data"};

    FeatureRoute r;
    for (const auto& name : order) {
        if (name.find(':') != std::string::npos) {
            r.perf.emplace_back(name, tracefsPath(name));      // kernel/fs tracepoint
        } else if (kDisk.count(name)) {
            r.disk.push_back(name);
        } else if (kResource.count(name)) {
            r.resource.push_back(name);
        } else {
            r.perf.emplace_back(name, "syscalls/sys_enter_" + name);  // syscall
        }
    }
    return r;
}

}  // namespace hids
