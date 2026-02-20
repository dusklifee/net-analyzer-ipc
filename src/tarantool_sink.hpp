#pragma once

#include <cstdint>
#include <string>
#include <atomic>

namespace netgate {

class TarantoolSink {
public:
    TarantoolSink();
    ~TarantoolSink();

    bool connect(const std::string& host = "127.0.0.1", int port = 3301,
                 const std::string& user = "netgate",
                 const std::string& password = "netgate_pass");

    void push_stats(uint64_t total_packets, uint64_t total_bytes,
                    double avg_size, uint32_t min_size, uint32_t max_size,
                    double pps, double throughput_mbps);

    bool is_connected() const;
    void disconnect();

private:
    struct Impl;
    Impl* impl_{nullptr};
    std::atomic<bool> connected_{false};
};

} // namespace netgate
