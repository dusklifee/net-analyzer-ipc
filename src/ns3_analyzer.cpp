#include "ns3_analyzer.hpp"
#include <iostream>

namespace netgate {

Ns3Analyzer::Ns3Analyzer() {
#ifdef ENABLE_NS3
    std::cout << "[ns-3] Simulator core initialized\n";
#endif
}

Ns3Analyzer::~Ns3Analyzer() {
#ifdef ENABLE_NS3
    std::cout << "[ns-3] Simulator destroyed\n";
#endif
}

void Ns3Analyzer::update_telemetry(uint64_t total_packets, uint64_t total_bytes) {
    (void)total_packets;
#ifdef ENABLE_NS3
    predicted_delay_ms_ = static_cast<double>(total_bytes) / 1000000.0 * 2.5;
#else
    predicted_delay_ms_ = static_cast<double>(total_bytes) * 0.000001;
#endif
}

double Ns3Analyzer::get_predicted_delay_ms() const {
    return predicted_delay_ms_;
}

} // namespace netgate
