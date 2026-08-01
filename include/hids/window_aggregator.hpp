// window_aggregator.hpp -- turns the merged {name -> value} map produced by the
// collectors into a raw feature vector in exactly FEATURE_ORDER. Missing names
// (e.g. a tracepoint that didn't resolve) become 0.0, preserving fixed
// dimensionality just as the training pipeline imputed missing values with 0.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace hids {

class WindowAggregator {
public:
    explicit WindowAggregator(std::vector<std::string> order)
        : order_(std::move(order)) {}

    // Build the raw (unnormalized) vector; the scaler is applied later by the
    // inference engine.
    std::vector<float> assemble(const std::map<std::string, double>& merged) const {
        std::vector<float> v(order_.size(), 0.0f);
        for (std::size_t i = 0; i < order_.size(); ++i) {
            auto it = merged.find(order_[i]);
            if (it != merged.end()) v[i] = static_cast<float>(it->second);
        }
        return v;
    }

    const std::vector<std::string>& order() const { return order_; }

private:
    std::vector<std::string> order_;
};

}  // namespace hids
