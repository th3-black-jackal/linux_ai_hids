// main.cpp -- HIDS service entrypoint.
//
//   hids [--config PATH] [--check] [--dump-features]
//
//   --config PATH     config file (default /etc/hids/hids.conf)
//   --check           print feature routing + unresolved tracepoints, then exit
//                     (use on the target / under qemu-system to verify parity)
//   --dump-features   also emit one CSV row per window to stdout (for overlaying
//                     live features on the training distribution)
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "hids/config.hpp"
#include "hids/hids_service.hpp"

static void onSignal(int) { hids::HidsService::requestStop(); }

int main(int argc, char** argv) {
    std::string config_path = "/etc/hids/hids.conf";
    bool check = false, dump_features = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) config_path = argv[++i];
        else if (a == "--check") check = true;
        else if (a == "--dump-features") dump_features = true;
        else if (a == "--help" || a == "-h") {
            std::printf("usage: hids [--config PATH] [--check] [--dump-features]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return 2;
        }
    }

    bool opened = false;
    hids::Config cfg = hids::Config::load(config_path, &opened);
    if (!opened)
        std::fprintf(stderr, "config: %s not found; using defaults\n",
                     config_path.c_str());

    try {
        hids::HidsService svc(cfg);
        svc.setup();
        if (check) { svc.printRoutes(); return 0; }

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
        std::fprintf(stderr, "hids: host=%s model=%s window=%ds -> mqtt %s:%d\n",
                     cfg.host.c_str(), cfg.model_path.c_str(), cfg.window_sec,
                     cfg.mqtt_host.c_str(), cfg.mqtt_port);
        svc.run(dump_features);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
