// meta.hpp -- thin accessors over the generated model_meta.hpp arrays so the
// rest of the code works with std::string/std::vector instead of C arrays.
#pragma once

#include <string>
#include <vector>

#include "model_meta.hpp"   // MODEL_N_FEATURES, MODEL_N_CLASSES, FEATURE_ORDER[], CLASS_NAMES[]

namespace hids {

inline std::vector<std::string> featureOrder() {
    return std::vector<std::string>(FEATURE_ORDER, FEATURE_ORDER + MODEL_N_FEATURES);
}

inline std::vector<std::string> classNames() {
    return std::vector<std::string>(CLASS_NAMES, CLASS_NAMES + MODEL_N_CLASSES);
}

inline int metaFeatureCount() { return MODEL_N_FEATURES; }
inline int metaClassCount()   { return MODEL_N_CLASSES; }

}  // namespace hids
