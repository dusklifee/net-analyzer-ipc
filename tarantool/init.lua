#!/usr/bin/env tarantool

local log     = require('log')
local fiber   = require('fiber')
local json    = require('json')
local console = require('console')

local script_rel     = debug.getinfo(1, 'S').source:sub(2)
local script_dir_rel = script_rel:match('(.*/)')  or './'
local handle         = io.popen('realpath ' .. script_dir_rel)
local SCRIPT_DIR     = handle:read('*l') .. '/'
handle:close()

local CONFIG = {
    listen              = '127.0.0.1:3301',
    memtx_memory        = 128 * 1024 * 1024,
    log_level           = 5,
    work_dir            = '/tmp/netgate_tarantool',
    checkpoint_interval = 60,
}

os.execute('mkdir -p ' .. CONFIG.work_dir)

box.cfg({
    listen              = CONFIG.listen,
    memtx_memory        = CONFIG.memtx_memory,
    log_level           = CONFIG.log_level,
    work_dir            = CONFIG.work_dir,
    checkpoint_interval = CONFIG.checkpoint_interval,
})

-- packet_log: per-packet data, ring buffer capped at 100K
local packet_log = box.schema.space.create('packet_log', {
    if_not_exists = true, engine = 'memtx',
})
packet_log:format({
    { name = 'id',           type = 'unsigned' },
    { name = 'packet_id',    type = 'unsigned' },
    { name = 'size_bytes',   type = 'unsigned' },
    { name = 'timestamp_ns', type = 'unsigned' },
    { name = 'worker_id',    type = 'integer'  },
    { name = 'created_at',   type = 'number'   },
})
packet_log:create_index('primary', { parts = {'id'}, if_not_exists = true })
packet_log:create_index('by_time', { parts = {'created_at'}, unique = false, if_not_exists = true })
packet_log:create_index('by_size', { parts = {'size_bytes'}, unique = false, if_not_exists = true })

-- flow_stats: aggregated per-second metrics
local flow_stats = box.schema.space.create('flow_stats', {
    if_not_exists = true, engine = 'memtx',
})
flow_stats:format({
    { name = 'id',              type = 'unsigned' },
    { name = 'interval_start',  type = 'number'   },
    { name = 'total_packets',   type = 'unsigned'  },
    { name = 'total_bytes',     type = 'unsigned'  },
    { name = 'avg_packet_size', type = 'number'    },
    { name = 'min_packet_size', type = 'unsigned'  },
    { name = 'max_packet_size', type = 'unsigned'  },
    { name = 'packets_per_sec', type = 'number'    },
    { name = 'throughput_mbps', type = 'number'    },
})
flow_stats:create_index('primary',     { parts = {'id'}, if_not_exists = true })
flow_stats:create_index('by_interval', { parts = {'interval_start'}, unique = false, if_not_exists = true })

-- alerts: threshold violations and anomalies
local alerts = box.schema.space.create('alerts', {
    if_not_exists = true, engine = 'memtx',
})
alerts:format({
    { name = 'id',          type = 'unsigned' },
    { name = 'alert_type',  type = 'string'   },
    { name = 'severity',    type = 'string'   },
    { name = 'message',     type = 'string'   },
    { name = 'value',       type = 'number'   },
    { name = 'threshold',   type = 'number'   },
    { name = 'created_at',  type = 'number'   },
})
alerts:create_index('primary',     { parts = {'id'}, if_not_exists = true })
alerts:create_index('by_severity', { parts = {'severity'}, unique = false, if_not_exists = true })
alerts:create_index('by_type',     { parts = {'alert_type'}, unique = false, if_not_exists = true })

box.schema.sequence.create('packet_seq', { if_not_exists = true })
box.schema.sequence.create('stats_seq',  { if_not_exists = true })
box.schema.sequence.create('alert_seq',  { if_not_exists = true })

local tnt_pass = os.getenv('NETGATE_TNT_PASS') or 'netgate_pass'
box.schema.user.create('netgate', { password = tnt_pass, if_not_exists = true })
box.schema.user.grant('netgate', 'read,write,execute', 'universe', nil, { if_not_exists = true })

local metrics = dofile(SCRIPT_DIR .. 'metrics.lua')
local cleanup = dofile(SCRIPT_DIR .. 'cleanup.lua')

function push_packet(packet_id, size_bytes, timestamp_ns, worker_id)
    local id = box.sequence.packet_seq:next()
    local now = fiber.time()
    box.space.packet_log:insert({ id, packet_id, size_bytes, timestamp_ns, worker_id, now })
    metrics.check_packet_alert(size_bytes, now)
    return id
end

function push_stats(total_packets, total_bytes, avg_size, min_size, max_size, pps, throughput)
    local id = box.sequence.stats_seq:next()
    local now = fiber.time()
    box.space.flow_stats:insert({
        id, now, total_packets, total_bytes, avg_size, min_size, max_size, pps, throughput
    })
    metrics.check_traffic_alert(pps, throughput, now)
    return id
end

function get_recent_stats(limit)  return metrics.get_recent_stats(limit or 10)  end
function get_top_packets(limit)   return metrics.get_top_packets(limit or 10)    end
function get_recent_alerts(sec)   return metrics.get_recent_alerts(sec or 300)   end
function get_dashboard()          return metrics.get_dashboard()                 end

cleanup.start()
console.listen('127.0.0.1:3302')

log.info('NetGate Tarantool ready | iproto: %s | console: 127.0.0.1:3302', CONFIG.listen)
