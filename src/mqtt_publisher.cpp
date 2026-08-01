#include "hids/mqtt_publisher.hpp"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace hids {

MqttPublisher::~MqttPublisher() { disconnect(); }

void MqttPublisher::disconnect() {
    if (fd_ >= 0) {
        // Best-effort DISCONNECT (0xE0 0x00), then close.
        const std::uint8_t pkt[2] = {0xE0, 0x00};
        sendAll(pkt, sizeof(pkt));
        ::close(fd_);
        fd_ = -1;
    }
}

// MQTT remaining-length: 7 bits per byte, MSB = continuation.
void MqttPublisher::putRemainingLength(std::vector<std::uint8_t>& b, std::size_t len) {
    do {
        std::uint8_t byte = len & 0x7F;
        len >>= 7;
        if (len) byte |= 0x80;
        b.push_back(byte);
    } while (len);
}

void MqttPublisher::putString(std::vector<std::uint8_t>& b, const std::string& s) {
    b.push_back(static_cast<std::uint8_t>((s.size() >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(s.size() & 0xFF));
    b.insert(b.end(), s.begin(), s.end());
}

bool MqttPublisher::sendAll(const std::uint8_t* p, std::size_t n) {
    std::size_t off = 0;
    while (off < n) {
        ssize_t w = ::send(fd_, p + off, n - off, MSG_NOSIGNAL);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return false;
        }
        off += static_cast<std::size_t>(w);
    }
    return true;
}

bool MqttPublisher::recvExact(std::uint8_t* p, std::size_t n, int timeout_ms) {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::size_t off = 0;
    while (off < n) {
        ssize_t r = ::recv(fd_, p + off, n - off, 0);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            return false;   // timeout or closed
        }
        off += static_cast<std::size_t>(r);
    }
    return true;
}

bool MqttPublisher::connect() {
    disconnect();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port = std::to_string(opt_.port);
    if (::getaddrinfo(opt_.host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
        return false;

    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) return false;
    fd_ = fd;

    // --- CONNECT ---
    std::vector<std::uint8_t> vh;
    putString(vh, "MQTT");            // protocol name
    vh.push_back(0x04);               // protocol level 4 (3.1.1)

    std::uint8_t flags = 0x02;        // clean session
    if (!opt_.username.empty()) flags |= 0x80;
    if (!opt_.password.empty()) flags |= 0x40;
    vh.push_back(flags);

    vh.push_back(static_cast<std::uint8_t>((opt_.keepalive >> 8) & 0xFF));
    vh.push_back(static_cast<std::uint8_t>(opt_.keepalive & 0xFF));

    std::vector<std::uint8_t> payload;
    putString(payload, opt_.client_id);
    if (!opt_.username.empty()) putString(payload, opt_.username);
    if (!opt_.password.empty()) putString(payload, opt_.password);

    std::vector<std::uint8_t> pkt;
    pkt.push_back(0x10);              // CONNECT
    putRemainingLength(pkt, vh.size() + payload.size());
    pkt.insert(pkt.end(), vh.begin(), vh.end());
    pkt.insert(pkt.end(), payload.begin(), payload.end());

    if (!sendAll(pkt.data(), pkt.size())) { disconnect(); return false; }

    // --- CONNACK: 0x20, len=2, session-present, return-code ---
    std::uint8_t ack[4];
    if (!recvExact(ack, 4, opt_.timeout_ms)) { disconnect(); return false; }
    if (ack[0] != 0x20 || ack[1] != 0x02 || ack[3] != 0x00) {  // rc 0 = accepted
        disconnect();
        return false;
    }
    return true;
}

bool MqttPublisher::ensureConnected() {
    if (fd_ >= 0) return true;
    return connect();
}

bool MqttPublisher::publish(const std::string& topic, const std::string& payload,
                            bool retain) {
    if (!ensureConnected()) return false;

    // PUBLISH, QoS 0 -> no packet identifier, no PUBACK.
    std::vector<std::uint8_t> vh;
    putString(vh, topic);

    std::vector<std::uint8_t> pkt;
    pkt.push_back(static_cast<std::uint8_t>(0x30 | (retain ? 0x01 : 0x00)));
    putRemainingLength(pkt, vh.size() + payload.size());
    pkt.insert(pkt.end(), vh.begin(), vh.end());
    pkt.insert(pkt.end(), payload.begin(), payload.end());

    if (!sendAll(pkt.data(), pkt.size())) {
        disconnect();                 // drop; reconnect on next publish
        return false;
    }
    return true;
}

}  // namespace hids
