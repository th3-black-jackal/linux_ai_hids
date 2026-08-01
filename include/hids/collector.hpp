// collector.hpp -- abstract source of behavioral features. Each sample() call
// returns the values accumulated since the previous call (one 30s window),
// keyed by the exact FEATURE_ORDER name.
#pragma once

#include <map>
#include <string>

namespace hids {

class ICollector {
public:
    virtual ~ICollector() = default;

    // Insert feature_name -> value for the window that just elapsed.
    virtual void sample(std::map<std::string, double>& out) = 0;

    virtual const char* name() const = 0;
};

}  // namespace hids
