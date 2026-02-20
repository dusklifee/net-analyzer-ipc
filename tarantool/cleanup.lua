local fiber = require('fiber')
local log   = require('log')

local cleanup = {}

local MAX_PACKET_LOG   = 100000
local STATS_TTL_SEC    = 3600
local ALERTS_TTL_SEC   = 7200
local CLEANUP_INTERVAL = 30

local function trim_packet_log()
    local space = box.space.packet_log
    local current_len = space:len()
    if current_len <= MAX_PACKET_LOG then return 0 end

    local to_delete = current_len - MAX_PACKET_LOG
    local deleted = 0
    for _, tuple in space.index.primary:pairs(nil, { iterator = box.index.GE }) do
        space:delete(tuple[1])
        deleted = deleted + 1
        if deleted >= to_delete then break end
    end
    return deleted
end

local function trim_flow_stats()
    local space = box.space.flow_stats
    local cutoff = fiber.time() - STATS_TTL_SEC
    local deleted = 0
    for _, tuple in space.index.by_interval:pairs(nil, { iterator = box.index.GE }) do
        if tuple[2] >= cutoff then break end
        space:delete(tuple[1])
        deleted = deleted + 1
    end
    return deleted
end

local function trim_alerts()
    local space = box.space.alerts
    local cutoff = fiber.time() - ALERTS_TTL_SEC
    local deleted = 0
    for _, tuple in space.index.primary:pairs(nil, { iterator = box.index.GE }) do
        if tuple[7] >= cutoff then break end
        space:delete(tuple[1])
        deleted = deleted + 1
    end
    return deleted
end

local function cleanup_loop()
    while true do
        fiber.sleep(CLEANUP_INTERVAL)
        local ok, err = pcall(function()
            local d1 = trim_packet_log()
            local d2 = trim_flow_stats()
            local d3 = trim_alerts()
            if d1 + d2 + d3 > 0 then
                log.info('[cleanup] removed %d packet_log, %d flow_stats, %d alerts', d1, d2, d3)
            end
        end)
        if not ok then
            log.error('[cleanup] %s', tostring(err))
        end
    end
end

function cleanup.start()
    fiber.create(cleanup_loop)
end

function force_cleanup()
    return {
        packet_log_deleted = trim_packet_log(),
        flow_stats_deleted = trim_flow_stats(),
        alerts_deleted     = trim_alerts(),
    }
end

return cleanup
