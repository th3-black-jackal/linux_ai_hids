#include "hids/hids_service.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

#include "hids/detection_event.hpp"
#include "hids/feature_routing.hpp"
#include "hids/meta.hpp"
#include "hids/perf_collector.hpp"
#include "hids/proc_collector.hpp"
#include "hids/resource_collector.hpp"

namespace hids {

namespace {
std::atomic<bool> g_stop{false};
}

void HidsService::requestStop() { g_stop.store(true); }

HidsService::HidsService(Config cfg) : cfg_(std::move(cfg)) {}

void HidsService::setup() {
    engine_.load(cfg_.model_path);
    const Model& m = engine_.model();

    order_ = featureOrder();
    if (m.n_in != static_cast<int>(order_.size()))
        throw std::runtime_error(
            "model n_in (" + std::to_string(m.n_in) +
            ") != FEATURE_ORDER length (" + std::to_string(order_.size()) +
            "); model.bin and model_meta.hpp are out of sync");
    if (m.n_out != metaClassCount())
        throw std::runtime_error("model n_out != CLASS_NAMES length");

    const FeatureRoute route = routeFeatures(order_);

    // perf: syscalls + kernel/fs tracepoints
    if (!route.perf.empty()) {
        auto perf = std::make_unique<PerfCollector>(route.perf);
        missing_tracepoints_ = perf->missing();
        collectors_.push_back(std::move(perf));
    }
    // /proc/diskstats-derived I/O features
    if (!route.disk.empty())
        collectors_.push_back(std::make_unique<ProcCollector>());
    // resource feature(s)
    if (!route.resource.empty())
        collectors_.push_back(std::make_unique<ResourceCollector>());

    agg_ = std::make_unique<WindowAggregator>(order_);

    const int normal_class = 0;  // CLASS_NAMES[0] == "normal"
    deb_ = std::make_unique<Debouncer>(normal_class, cfg_.alert_streak,
                                       cfg_.min_confidence);

    MqttPublisher::Options mo;
    mo.host = cfg_.mqtt_host;
    mo.port = cfg_.mqtt_port;
    mo.client_id = cfg_.mqtt_client_id;
    mo.username = cfg_.mqtt_username;
    mo.password = cfg_.mqtt_password;
    mo.keepalive = cfg_.mqtt_keepalive;
    mqtt_ = std::make_unique<MqttPublisher>(mo);
}

void HidsService::printRoutes() const {
    const FeatureRoute route = routeFeatures(featureOrder());
    std::printf("perf tracepoints (%zu):\n", route.perf.size());
    for (const auto& [feat, path] : route.perf)
        std::printf("  %-42s <- %s\n", feat.c_str(), path.c_str());
    std::printf("diskstats (%zu):", route.disk.size());
    for (const auto& d : route.disk) std::printf(" %s", d.c_str());
    std::printf("\nresource (%zu):", route.resource.size());
    for (const auto& r : route.resource) std::printf(" %s", r.c_str());
    std::printf("\n\nunresolved tracepoints on THIS kernel (%zu):\n",
                missing_tracepoints_.size());
    for (const auto& mtp : missing_tracepoints_)
        std::printf("  MISSING %s  (will read 0 -> feed 0 to the model)\n", mtp.c_str());
    if (missing_tracepoints_.empty())
        std::printf("  (none -- all tracepoints resolved)\n");
}

void HidsService::run(bool dump_features) {
    using clock = std::chrono::steady_clock;
    const auto window = std::chrono::seconds(cfg_.window_sec);
    const std::vector<std::string> classes = classNames();

    if (!missing_tracepoints_.empty())
        std::fprintf(stderr, "warning: %zu tracepoint(s) unresolved on this kernel; "
                             "run --check to list them\n",
                     missing_tracepoints_.size());

    if (mqtt_->connect())
        std::fprintf(stderr, "mqtt: connected to %s:%d\n",
                     cfg_.mqtt_host.c_str(), cfg_.mqtt_port);
    else
        std::fprintf(stderr, "mqtt: broker unreachable at %s:%d (will retry)\n",
                     cfg_.mqtt_host.c_str(), cfg_.mqtt_port);

    // Prime collector baselines so the first published window is a real delta.
    { std::map<std::string, double> discard; for (auto& c : collectors_) c->sample(discard); }
    std::this_thread::sleep_for(window);

    if (dump_features) {
        for (std::size_t i = 0; i < order_.size(); ++i)
            std::printf("%s%s", order_[i].c_str(), i + 1 < order_.size() ? "," : "");
        std::printf(",class,confidence\n");
    }

    while (!g_stop.load()) {
        const auto t0 = clock::now();

        std::map<std::string, double> merged;
        for (auto& c : collectors_) c->sample(merged);

        const std::vector<float> raw = agg_->assemble(merged);
        const InferenceResult res = engine_.infer(raw);

        DetectionEvent ev;
        ev.timestamp = isoNow();
        ev.classIdx = res.classIdx;
        ev.className = (res.classIdx < static_cast<int>(classes.size()))
                           ? classes[res.classIdx] : "?";
        ev.confidence = res.confidence;
        ev.host = cfg_.host;
        ev.features = res.normalized;
        ev.alert = deb_->update(res.classIdx, res.confidence);

        if (cfg_.publish_every_window)
            mqtt_->publish(cfg_.eventsTopic(), ev.toJson(cfg_.include_features));
        if (ev.alert) {
            mqtt_->publish(cfg_.alertsTopic(), ev.toJson(cfg_.include_features));
            std::fprintf(stderr, "ALERT %s class=%s conf=%.3f\n",
                         ev.timestamp.c_str(), ev.className.c_str(), ev.confidence);
        }

        if (dump_features) {
            for (std::size_t i = 0; i < res.normalized.size(); ++i)
                std::printf("%.6g,", res.normalized[i]);
            std::printf("%d,%.4f\n", res.classIdx, res.confidence);
            std::fflush(stdout);
        }

        // Sleep the remainder of the window, waking early if asked to stop.
        auto next = t0 + window;
        while (!g_stop.load() && clock::now() < next)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    mqtt_->disconnect();
    std::fprintf(stderr, "hids: stopped\n");
}

}  // namespace hids
