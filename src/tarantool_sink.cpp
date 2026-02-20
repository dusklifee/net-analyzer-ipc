#include "tarantool_sink.hpp"
#include <iostream>

#ifdef ENABLE_TARANTOOL

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

namespace netgate {

struct TarantoolSink::Impl {
    int sockfd = -1;
    std::string host;
    int port = 3302;

    bool send_lua(const std::string& lua_code) {
        if (sockfd < 0) return false;
        std::string cmd = lua_code + "\n";
        ssize_t total = 0;
        ssize_t len = static_cast<ssize_t>(cmd.size());
        while (total < len) {
            ssize_t sent = ::send(sockfd, cmd.data() + total, len - total, MSG_NOSIGNAL);
            if (sent <= 0) return false;
            total += sent;
        }
        return true;
    }

    void drain_response() {
        char buf[4096];
        struct timeval tv{0, 30000};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::recv(sockfd, buf, sizeof(buf), 0);
    }
};

TarantoolSink::TarantoolSink() : impl_(new Impl) {}

TarantoolSink::~TarantoolSink() {
    disconnect();
    delete impl_;
}

bool TarantoolSink::connect(const std::string& host, int port,
                             const std::string&, const std::string&) {
    impl_->host = host;
    impl_->port = port + 1; // console port = iproto + 1

    impl_->sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->sockfd < 0) {
        std::cerr << "[Tarantool] Failed to create socket\n";
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(impl_->port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::connect(impl_->sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Tarantool] Connection failed to "
                  << host << ":" << impl_->port << "\n";
        ::close(impl_->sockfd);
        impl_->sockfd = -1;
        return false;
    }

    impl_->drain_response(); // skip greeting banner

    connected_.store(true, std::memory_order_relaxed);
    std::cout << "[Tarantool] Connected to " << host << ":" << impl_->port << "\n";
    return true;
}

void TarantoolSink::push_stats(uint64_t total_packets, uint64_t total_bytes,
                                double avg_size, uint32_t min_size, uint32_t max_size,
                                double pps, double throughput_mbps) {
    if (!connected_.load(std::memory_order_relaxed)) return;

    std::ostringstream cmd;
    cmd << "push_stats("
        << total_packets << ", "
        << total_bytes << ", "
        << avg_size << ", "
        << min_size << ", "
        << max_size << ", "
        << pps << ", "
        << throughput_mbps << ")";

    if (!impl_->send_lua(cmd.str())) {
        std::cerr << "[Tarantool] Send failed, disconnecting\n";
        connected_.store(false, std::memory_order_relaxed);
        return;
    }

    impl_->drain_response();
}

bool TarantoolSink::is_connected() const {
    return connected_.load(std::memory_order_relaxed);
}

void TarantoolSink::disconnect() {
    if (impl_ && impl_->sockfd >= 0) {
        ::close(impl_->sockfd);
        impl_->sockfd = -1;
        connected_.store(false, std::memory_order_relaxed);
    }
}

} // namespace netgate

#else

namespace netgate {

struct TarantoolSink::Impl {};
TarantoolSink::TarantoolSink() {}
TarantoolSink::~TarantoolSink() = default;
bool TarantoolSink::connect(const std::string&, int, const std::string&, const std::string&) { return false; }
void TarantoolSink::push_stats(uint64_t, uint64_t, double, uint32_t, uint32_t, double, double) {}
bool TarantoolSink::is_connected() const { return false; }
void TarantoolSink::disconnect() {}

} // namespace netgate

#endif
