// resource_collector.hpp -- emits the single "resource" feature, seconds_RES_data.
//
// PARITY CAVEAT: the exact training-time semantics of seconds_RES_data are not
// recoverable from the released artifacts (schema, model_meta, sample). The
// feature-engineering repo's resource module sampled every 5s; "seconds" most
// plausibly tracks elapsed monitoring time. We emit monotonic seconds since the
// service started. This is a BEST-GUESS and MUST be validated by overlaying the
// live value on the training distribution for this column before trusting any
// detection that leans on it. If the overlay disagrees, fix here or retrain
// without this feature.
#pragma once

#include <chrono>
#include <map>
#include <string>

#include "hids/collector.hpp"

namespace hids {

class ResourceCollector : public ICollector {
public:
    ResourceCollector() : start_(std::chrono::steady_clock::now()) {}

    void sample(std::map<std::string, double>& out) override {
        using namespace std::chrono;
        const double secs =
            duration<double>(steady_clock::now() - start_).count();
        out["seconds_RES_data"] = secs;
    }

    const char* name() const override { return "resource"; }

private:
    std::chrono::steady_clock::time_point start_;
};

}  // namespace hids
