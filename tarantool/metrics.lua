local fiber = require('fiber')
local log   = require('log')

local metrics = {}

local THRESHOLDS = {
    large_packet_bytes   = 1400,
    high_pps             = 8000,
    high_throughput_mbps = 50.0,
    spike_ratio          = 3.0,
}

local pps_window = {}
local PPS_WINDOW_SIZE = 10

function metrics.check_packet_alert(size_bytes, now)
    if size_bytes >= THRESHOLDS.large_packet_bytes then
        box.space.alerts:insert({
            box.sequence.alert_seq:next(),
            'large_packet', 'info',
            string.format('Large packet: %d bytes (threshold: %d)',
                          size_bytes, THRESHOLDS.large_packet_bytes),
            size_bytes, THRESHOLDS.large_packet_bytes, now,
        })
    end
end

function metrics.check_traffic_alert(pps, throughput_mbps, now)
    if pps >= THRESHOLDS.high_pps then
        box.space.alerts:insert({
            box.sequence.alert_seq:next(),
            'high_traffic', 'warning',
            string.format('High PPS: %.0f (threshold: %d)', pps, THRESHOLDS.high_pps),
            pps, THRESHOLDS.high_pps, now,
        })
    end

    if throughput_mbps >= THRESHOLDS.high_throughput_mbps then
        box.space.alerts:insert({
            box.sequence.alert_seq:next(),
            'high_traffic', 'critical',
            string.format('High throughput: %.2f Mbps (threshold: %.0f)',
                          throughput_mbps, THRESHOLDS.high_throughput_mbps),
            throughput_mbps, THRESHOLDS.high_throughput_mbps, now,
        })
    end

    table.insert(pps_window, pps)
    if #pps_window > PPS_WINDOW_SIZE then
        table.remove(pps_window, 1)
    end

    if #pps_window >= 3 then
        local sum = 0
        for _, v in ipairs(pps_window) do sum = sum + v end
        local avg = sum / #pps_window

        if avg > 0 and pps / avg >= THRESHOLDS.spike_ratio then
            box.space.alerts:insert({
                box.sequence.alert_seq:next(),
                'spike', 'warning',
                string.format('Traffic spike: PPS=%.0f, avg=%.0f (x%.1f)', pps, avg, pps / avg),
                pps, avg, now,
            })
            log.warn('SPIKE: pps=%.0f avg=%.0f ratio=%.1f', pps, avg, pps / avg)
        end
    end
end

function metrics.get_recent_stats(limit)
    local result = {}
    local count = 0
    for _, t in box.space.flow_stats.index.primary:pairs(nil, { iterator = box.index.REQ }) do
        table.insert(result, {
            id = t[1], interval_start = t[2], total_packets = t[3], total_bytes = t[4],
            avg_packet_size = t[5], min_packet_size = t[6], max_packet_size = t[7],
            packets_per_sec = t[8], throughput_mbps = t[9],
        })
        count = count + 1
        if count >= limit then break end
    end
    return result
end

function metrics.get_top_packets(limit)
    local result = {}
    local count = 0
    for _, t in box.space.packet_log.index.by_size:pairs(nil, { iterator = box.index.REQ }) do
        table.insert(result, {
            id = t[1], packet_id = t[2], size_bytes = t[3],
            timestamp_ns = t[4], worker_id = t[5], created_at = t[6],
        })
        count = count + 1
        if count >= limit then break end
    end
    return result
end

function metrics.get_recent_alerts(seconds)
    local cutoff = fiber.time() - seconds
    local result = {}
    for _, t in box.space.alerts.index.primary:pairs(nil, { iterator = box.index.REQ }) do
        if t[7] < cutoff then break end
        table.insert(result, {
            id = t[1], alert_type = t[2], severity = t[3],
            message = t[4], value = t[5], threshold = t[6], created_at = t[7],
        })
    end
    return result
end

function metrics.get_dashboard()
    local now = fiber.time()

    local last_stat = nil
    for _, t in box.space.flow_stats.index.primary:pairs(nil, { iterator = box.index.REQ }) do
        last_stat = {
            total_packets = t[3], total_bytes = t[4], avg_packet_size = t[5],
            packets_per_sec = t[8], throughput_mbps = t[9],
        }
        break
    end

    local recent_alert_count = 0
    local cutoff = now - 300
    for _, t in box.space.alerts.index.primary:pairs(nil, { iterator = box.index.REQ }) do
        if t[7] < cutoff then break end
        recent_alert_count = recent_alert_count + 1
    end

    local severity_counts = { info = 0, warning = 0, critical = 0 }
    for _ in box.space.alerts.index.by_severity:pairs('info')     do severity_counts.info = severity_counts.info + 1 end
    for _ in box.space.alerts.index.by_severity:pairs('warning')  do severity_counts.warning = severity_counts.warning + 1 end
    for _ in box.space.alerts.index.by_severity:pairs('critical') do severity_counts.critical = severity_counts.critical + 1 end

    return {
        timestamp            = now,
        total_packets_logged = box.space.packet_log:len(),
        total_stats_entries  = box.space.flow_stats:len(),
        total_alerts         = box.space.alerts:len(),
        recent_alerts_5min   = recent_alert_count,
        severity_counts      = severity_counts,
        last_flow_stats      = last_stat,
        thresholds           = THRESHOLDS,
    }
end

return metrics
