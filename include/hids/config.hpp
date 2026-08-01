// config.hpp -- runtime configuration, loaded from a key=value file.
#pragma once

#include <string>

namespace hids {

struct Config {
    // model / windowing
    std::string model_path   = "/etc/hids/model.bin";
    int         window_sec   = 30;

    // identity
    std::string host;                       // default: gethostname()

    // MQTT (Mosquitto) telemetry
    std::string mqtt_host       = "127.0.0.1";
    int         mqtt_port       = 1883;
    std::string mqtt_client_id;             // default: "hids-" + host
    std::string mqtt_username;
    std::string mqtt_password;
    int         mqtt_keepalive  = 60;
    std::string mqtt_topic_prefix = "hids"; // topics: <prefix>/<host>/{events,alerts}

    // publish policy
    bool        publish_every_window = true;   // else only alerts
    bool        include_features     = true;    // attach normalized vector to events

    // debounce / alerting
    float       min_confidence = 0.60f;
    int         alert_streak   = 2;            // consecutive windows to confirm

    // Load from file; unknown keys are ignored, missing keys keep defaults.
    // Returns false only if the path was given but could not be opened.
    static Config load(const std::string& path, bool* ok = nullptr);

    std::string eventsTopic() const { return mqtt_topic_prefix + "/" + host + "/events"; }
    std::string alertsTopic() const { return mqtt_topic_prefix + "/" + host + "/alerts"; }
};

}  // namespace hids
