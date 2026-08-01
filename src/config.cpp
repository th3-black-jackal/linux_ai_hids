#include "hids/config.hpp"

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace hids {

static std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string hostname() {
    char buf[256] = {0};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) return buf;
    return "unknown";
}

static bool toBool(const std::string& v) {
    std::string s = v;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

Config Config::load(const std::string& path, bool* ok) {
    Config c;
    bool opened = false;

    std::ifstream f(path);
    if (f) {
        opened = true;
        std::string line;
        while (std::getline(f, line)) {
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            const auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = trim(t.substr(0, eq));
            const std::string v = trim(t.substr(eq + 1));

            if      (k == "model_path")          c.model_path = v;
            else if (k == "window_sec")          c.window_sec = std::stoi(v);
            else if (k == "host")                c.host = v;
            else if (k == "mqtt_host")           c.mqtt_host = v;
            else if (k == "mqtt_port")           c.mqtt_port = std::stoi(v);
            else if (k == "mqtt_client_id")      c.mqtt_client_id = v;
            else if (k == "mqtt_username")       c.mqtt_username = v;
            else if (k == "mqtt_password")       c.mqtt_password = v;
            else if (k == "mqtt_keepalive")      c.mqtt_keepalive = std::stoi(v);
            else if (k == "mqtt_topic_prefix")   c.mqtt_topic_prefix = v;
            else if (k == "publish_every_window") c.publish_every_window = toBool(v);
            else if (k == "include_features")    c.include_features = toBool(v);
            else if (k == "min_confidence")      c.min_confidence = std::stof(v);
            else if (k == "alert_streak")        c.alert_streak = std::stoi(v);
        }
    }

    if (c.host.empty()) c.host = hostname();
    if (c.mqtt_client_id.empty()) c.mqtt_client_id = "hids-" + c.host;

    if (ok) *ok = opened;
    return c;
}

}  // namespace hids
