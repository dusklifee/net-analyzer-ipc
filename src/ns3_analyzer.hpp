#pragma once

#include <cstdint>

namespace netgate {

class Ns3Analyzer {
public:
    Ns3Analyzer();
    ~Ns3Analyzer();

    void update_telemetry(uint64_t total_packets, uint64_t total_bytes);
    double get_predicted_delay_ms() const;

private:
    double predicted_delay_ms_{0.0};
};

} // namespace netgate
