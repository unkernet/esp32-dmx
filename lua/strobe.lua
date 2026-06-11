--- Strobe with controlled frequency
--

local leds = 10
local control_universe = 1
local output_universe = 10

local delay = 150
local state = 0
local timer

local function tick()
    esp.dmx.send(output_universe, string.char(state):rep(leds * 3))
    state = (state == 0) and 100 or 0
end

local function arm()
    if timer then esp.clearTimer(timer) end
    timer = esp.setInterval(tick, delay)
end

-- Update frequency from channel 1 of control Universe; re-arm only when it changes.
esp.dmx.on(control_universe, function(data)
    local new_delay = string.byte(data, 1, 1) + 20
    if new_delay ~= delay then
        delay = new_delay
        arm()
    end
end)

arm()
