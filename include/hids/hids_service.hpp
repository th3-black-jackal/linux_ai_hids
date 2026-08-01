// hids_service.hpp -- owns the pipeline and drives the per-window loop:
// collect -> aggregate (FEATURE_ORDER) -> normalize+infer -> debounce ->
// publish to MQTT.
#pragma once

#include <memory>
#include <vector>

#include "hids/collector.hpp"
#include "hids/config.hpp"
#include "hids/debouncer.hpp"
#include "hids/inference_engine.hpp"
#include "hids/mqtt_publisher.hpp"
#include "hids/window_aggregator.hpp"

namespace hids {

class HidsService {
public:
    explicit HidsService(Config cfg);

    // Build collectors, load the model, verify the FEATURE_ORDER contract.
    // Throws std::runtime_error on unrecoverable setup errors.
    void setup();

    // Print resolved feature routing + any tracepoints missing on this kernel,
    // then return (used by --check for Tier-2 parity verification).
    void printRoutes() const;

    // Run the loop until stop() is called (SIGINT/SIGTERM). If dump_features,
    // also prints one CSV row per window (FEATURE_ORDER) to stdout.
    void run(bool dump_features);

    static void requestStop();   // signal-safe

private:
    Config                                    cfg_;
    InferenceEngine                           engine_;
    std::vector<std::unique_ptr<ICollector>>  collectors_;
    std::unique_ptr<WindowAggregator>         agg_;
    std::unique_ptr<Debouncer>                deb_;
    std::unique_ptr<MqttPublisher>            mqtt_;
    std::vector<std::string>                  order_;
    std::vector<std::string>                  missing_tracepoints_;
};

}  // namespace hids
