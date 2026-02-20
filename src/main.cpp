#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <csignal>
#include "PacketQueue.hpp"
#include "stats_aggregator.hpp"
#include "logger.hpp"
#include "ns3_analyzer.hpp"
#include "tarantool_sink.hpp"

using namespace netgate;
using namespace std::chrono_literals;

static StatsAggregator g_stats;
static std::atomic<bool> running{true};
static Logger g_log("gateway");

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        running.store(false, std::memory_order_relaxed);
    }
}

void producer_func(std::stop_token stoken, ThreadSafeQueue<Packet>& queue) {
    Logger log("producer");
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> size_dist(64, 1500);
    uint32_t id = 0;

    log.info("started");
    while (!stoken.stop_requested() && running.load(std::memory_order_relaxed)) {
        Packet p;
        p.id = ++id;
        p.size = size_dist(rng);
        p.timestamp_ns = std::chrono::steady_clock::now().time_since_epoch().count();
        queue.push(p);
        std::this_thread::sleep_for(100us);
    }
    log.info("stopped");
}

void worker_func(std::stop_token stoken, int worker_id, ThreadSafeQueue<Packet>& queue) {
    Logger log("worker-" + std::to_string(worker_id));
    log.info("started");

    while (!stoken.stop_requested() && running.load(std::memory_order_relaxed)) {
        Packet p = queue.pop();
        for (volatile int i = 0; i < 500; ++i) {}
        g_stats.record_packet(p.size, p.timestamp_ns);
    }
    log.info("stopped");
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    g_log.info("Starting NetGateLite (Ctrl+C to stop)");

    const char* fifo_path = "/tmp/netgate_stats.fifo";
    mkfifo(fifo_path, 0666);
    int fd = open(fifo_path, O_RDWR | O_NONBLOCK);
    if (fd == -1) {
        g_log.error("Failed to open FIFO: " + std::string(fifo_path));
        return 1;
    }
    g_log.info("IPC pipe opened: " + std::string(fifo_path));

    ThreadSafeQueue<Packet> queue(5000);
    std::jthread producer(producer_func, std::ref(queue));

    constexpr int NUM_WORKERS = 3;
    std::vector<std::jthread> workers;
    for (int i = 0; i < NUM_WORKERS; ++i)
        workers.emplace_back(worker_func, i, std::ref(queue));

    Ns3Analyzer ns3_env;
    TarantoolSink tnt_sink;
    tnt_sink.connect();

    StatsAggregator::Snapshot prev_snap = g_stats.take_snapshot();
    std::this_thread::sleep_for(1s);

    while (running.load(std::memory_order_relaxed)) {
        auto snap = g_stats.take_snapshot(&prev_snap);

        ns3_env.update_telemetry(snap.total_packets, snap.total_bytes);
        double delay = ns3_env.get_predicted_delay_ms();

        tnt_sink.push_stats(snap.total_packets, snap.total_bytes,
                            snap.avg_packet_size,
                            (snap.min_packet_size == UINT32_MAX ? 0 : snap.min_packet_size),
                            snap.max_packet_size, snap.pps, snap.throughput_mbps);

        std::string tnt_status = tnt_sink.is_connected() ? "connected" : "disconnected";
        std::string json = g_stats.to_json(snap);
        json.pop_back(); // remove trailing '}'
        json += ",\"predicted_delay_ms\":" + std::to_string(delay)
              + ",\"tarantool\":\"" + tnt_status + "\"}\n";
        write(fd, json.c_str(), json.length());

        g_log.info_fmt("pps=%.0f throughput=%.2f Mbps avg=%d bytes jitter=%.0f ns",
                       snap.pps, snap.throughput_mbps,
                       static_cast<int>(snap.avg_packet_size), snap.jitter_ns);

        prev_snap = snap;
        std::this_thread::sleep_for(1s);
    }

    g_log.info("shutting down...");
    producer.request_stop();
    for (auto& w : workers) w.request_stop();
    for (size_t i = 0; i < workers.size(); ++i)
        queue.push({0, 0, 0});

    close(fd);
    unlink(fifo_path);
    g_log.info("exited cleanly");
    return 0;
}
