#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <string>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace netgate {

class StatsAggregator {
public:
    static constexpr size_t HISTOGRAM_BUCKETS = 8;
    static constexpr uint32_t BUCKET_SIZE = 200; // bytes per bucket

    struct Snapshot {
        uint64_t total_packets;
        uint64_t total_bytes;
        double avg_packet_size;
        uint32_t min_packet_size;
        uint32_t max_packet_size;
        double pps;
        double throughput_mbps;
        double jitter_ns;
        std::array<uint64_t, HISTOGRAM_BUCKETS> size_histogram;
        std::chrono::steady_clock::time_point timestamp;
    };

    StatsAggregator() {
        reset();
    }

    void record_packet(uint32_t size_bytes, uint64_t timestamp_ns) {
        packets_.fetch_add(1, std::memory_order_relaxed);
        bytes_.fetch_add(size_bytes, std::memory_order_relaxed);

        update_min(min_size_, size_bytes);
        update_max(max_size_, size_bytes);

        size_t bucket = std::min(static_cast<size_t>(size_bytes / BUCKET_SIZE),
                                 HISTOGRAM_BUCKETS - 1);
        histogram_[bucket].fetch_add(1, std::memory_order_relaxed);

        uint64_t prev = last_timestamp_ns_.exchange(timestamp_ns, std::memory_order_relaxed);
        if (prev > 0 && timestamp_ns > prev) {
            uint64_t delta = timestamp_ns - prev;
            uint64_t old_jitter = jitter_sum_ns_.load(std::memory_order_relaxed);
            jitter_sum_ns_.store(old_jitter + delta, std::memory_order_relaxed);
            jitter_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    Snapshot take_snapshot(const Snapshot* prev = nullptr) const {
        Snapshot snap{};
        snap.timestamp = std::chrono::steady_clock::now();
        snap.total_packets = packets_.load(std::memory_order_relaxed);
        snap.total_bytes = bytes_.load(std::memory_order_relaxed);
        snap.min_packet_size = min_size_.load(std::memory_order_relaxed);
        snap.max_packet_size = max_size_.load(std::memory_order_relaxed);

        if (snap.total_packets > 0) {
            snap.avg_packet_size = static_cast<double>(snap.total_bytes) / snap.total_packets;
        }

        for (size_t i = 0; i < HISTOGRAM_BUCKETS; ++i) {
            snap.size_histogram[i] = histogram_[i].load(std::memory_order_relaxed);
        }

        uint64_t jcount = jitter_count_.load(std::memory_order_relaxed);
        if (jcount > 0) {
            snap.jitter_ns = static_cast<double>(jitter_sum_ns_.load(std::memory_order_relaxed)) / jcount;
        }

        if (prev) {
            auto elapsed = std::chrono::duration<double>(snap.timestamp - prev->timestamp);
            if (elapsed.count() > 0) {
                uint64_t dp = snap.total_packets - prev->total_packets;
                uint64_t db = snap.total_bytes - prev->total_bytes;
                snap.pps = static_cast<double>(dp) / elapsed.count();
                snap.throughput_mbps = static_cast<double>(db) * 8.0 / 1000000.0 / elapsed.count();
            }
        }

        return snap;
    }

    std::string format_histogram(const Snapshot& snap) const {
        std::ostringstream out;
        uint64_t max_val = *std::max_element(snap.size_histogram.begin(),
                                              snap.size_histogram.end());
        if (max_val == 0) max_val = 1;

        for (size_t i = 0; i < HISTOGRAM_BUCKETS; ++i) {
            uint32_t lo = i * BUCKET_SIZE;
            uint32_t hi = (i + 1) * BUCKET_SIZE - 1;
            if (i == HISTOGRAM_BUCKETS - 1) hi = 9999;

            int bar_len = static_cast<int>(snap.size_histogram[i] * 40 / max_val);
            out << "[" << lo << "-" << hi << "] ";
            for (int j = 0; j < bar_len; ++j) out << '#';
            out << " " << snap.size_histogram[i] << "\n";
        }
        return out.str();
    }

    std::string to_json(const Snapshot& snap) const {
        std::ostringstream out;
        out << "{"
            << "\"packets\":" << snap.total_packets
            << ",\"bytes\":" << snap.total_bytes
            << ",\"avg_size\":" << snap.avg_packet_size
            << ",\"min_size\":" << snap.min_packet_size
            << ",\"max_size\":" << snap.max_packet_size
            << ",\"pps\":" << snap.pps
            << ",\"throughput_mbps\":" << snap.throughput_mbps
            << ",\"jitter_ns\":" << snap.jitter_ns
            << ",\"histogram\":[";
        for (size_t i = 0; i < HISTOGRAM_BUCKETS; ++i) {
            if (i > 0) out << ",";
            out << snap.size_histogram[i];
        }
        out << "]}";
        return out.str();
    }

    void reset() {
        packets_.store(0, std::memory_order_relaxed);
        bytes_.store(0, std::memory_order_relaxed);
        min_size_.store(UINT32_MAX, std::memory_order_relaxed);
        max_size_.store(0, std::memory_order_relaxed);
        jitter_sum_ns_.store(0, std::memory_order_relaxed);
        jitter_count_.store(0, std::memory_order_relaxed);
        last_timestamp_ns_.store(0, std::memory_order_relaxed);
        for (auto& h : histogram_)
            h.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> packets_{0};
    std::atomic<uint64_t> bytes_{0};
    std::atomic<uint32_t> min_size_{UINT32_MAX};
    std::atomic<uint32_t> max_size_{0};
    std::atomic<uint64_t> jitter_sum_ns_{0};
    std::atomic<uint64_t> jitter_count_{0};
    std::atomic<uint64_t> last_timestamp_ns_{0};
    std::array<std::atomic<uint64_t>, HISTOGRAM_BUCKETS> histogram_;

    static void update_min(std::atomic<uint32_t>& target, uint32_t val) {
        uint32_t prev = target.load(std::memory_order_relaxed);
        while (val < prev && !target.compare_exchange_weak(prev, val, std::memory_order_relaxed)) {}
    }

    static void update_max(std::atomic<uint32_t>& target, uint32_t val) {
        uint32_t prev = target.load(std::memory_order_relaxed);
        while (val > prev && !target.compare_exchange_weak(prev, val, std::memory_order_relaxed)) {}
    }
};

} // namespace netgate
