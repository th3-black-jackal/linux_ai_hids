// inference_engine.hpp -- thin wrapper over Model that applies the baked-in
// min/max scaler and runs the forward pass. Keeps the normalized vector around
// so it can be attached to the telemetry event.
#pragma once

#include <string>
#include <vector>

#include "hids/model.hpp"

namespace hids {

struct InferenceResult {
    int                classIdx = 0;
    float              confidence = 0.0f;
    std::vector<float> normalized;   // scaled inputs, in FEATURE_ORDER
};

class InferenceEngine {
public:
    void load(const std::string& path) { model_ = Model::load(path); }

    const Model& model() const { return model_; }

    InferenceResult infer(const std::vector<float>& raw) const {
        InferenceResult r;
        r.normalized.resize(model_.n_in);
        model_.normalize(raw.data(), r.normalized.data());
        r.classIdx = model_.predict(r.normalized.data(), &r.confidence);
        return r;
    }

private:
    Model model_;
};

}  // namespace hids
