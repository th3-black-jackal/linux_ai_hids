// mqtt_publisher.hpp -- minimal MQTT 3.1.1 client, publish-only (QoS 0).
//
// Deliberately dependency-free (no paho/openssl) so the service stays a
// self-contained binary that cross-compiles to armhf without extra libraries,
// mirroring the hand-rolled inference core. Supports CONNECT with optional
// username/password and transparent reconnect on the next publish.
//
// NOTE: this is plaintext MQTT. Username/password without TLS is not
// confidential. TLS (wrapping the socket) is the production hardening step and
// is intentionally out of scope for this pass.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hids {

class MqttPublisher {
public:
    struct Options {
        std::string host = "127.0.0.1";
        int         port = 1883;
        std::string client_id = "hids";
        std::string username;
        std::string password;
        int         keepalive = 60;   // seconds
        int         timeout_ms = 3000; // connect / CONNACK timeout
    };

    explicit MqttPublisher(Options opt) : opt_(std::move(opt)) {}
    ~MqttPublisher();

    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    // Establishes TCP + MQTT session. Returns true on CONNACK accepted.
    bool connect();

    // Publishes payload to topic at QoS 0. Reconnects transparently if the link
    // dropped. Returns false if the broker is currently unreachable (the caller
    // logs and continues; telemetry loss must never stop detection).
    bool publish(const std::string& topic, const std::string& payload,
                 bool retain = false);

    bool connected() const { return fd_ >= 0; }
    void disconnect();

private:
    Options opt_;
    int     fd_ = -1;

    bool ensureConnected();
    bool sendAll(const std::uint8_t* p, std::size_t n);
    bool recvExact(std::uint8_t* p, std::size_t n, int timeout_ms);

    static void putRemainingLength(std::vector<std::uint8_t>& b, std::size_t len);
    static void putString(std::vector<std::uint8_t>& b, const std::string& s);
};

}  // namespace hids
