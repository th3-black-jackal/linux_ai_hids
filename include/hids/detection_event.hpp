// detection_event.hpp -- the per-window result produced by inference and
// consumed by the debouncer, the Wazuh logger, and the telemetry server.
#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace hids {

inline std::string isoNow() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\t': o += "\\t";  break;
            case '\r': o += "\\r";  break;
            default:   o += c;      break;
        }
    }
    return o;
}

struct DetectionEvent {
    std::string        timestamp;   // ISO-8601 UTC
    int                classIdx = 0;
    std::string        className;
    float              confidence = 0.0f;
    std::string        host;
    bool               alert = false;      // debouncer verdict
    std::vector<float> features;           // normalized, in FEATURE_ORDER

    std::string toJson(bool includeFeatures) const {
        char num[64];
        std::string j = "{";
        j += "\"timestamp\":\"" + jsonEscape(timestamp) + "\",";
        j += "\"class\":" + std::to_string(classIdx) + ",";
        j += "\"class_name\":\"" + jsonEscape(className) + "\",";
        std::snprintf(num, sizeof(num), "%.4f", confidence);
        j += "\"confidence\":" + std::string(num) + ",";
        j += "\"alert\":" + std::string(alert ? "true" : "false") + ",";
        j += "\"host\":\"" + jsonEscape(host) + "\"";
        if (includeFeatures) {
            j += ",\"features\":[";
            for (std::size_t i = 0; i < features.size(); ++i) {
                std::snprintf(num, sizeof(num), "%.6g", features[i]);
                j += num;
                if (i + 1 < features.size()) j += ",";
            }
            j += "]";
        }
        j += "}";
        return j;
    }
};

}  // namespace hids
