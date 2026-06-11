--- Turn on single led, controlled by channel 1 of another Universe
--

local leds = 50
local control_universe = 1
local output_universe = 10
local network = false -- do not output to websocket

local function draw(pos)
    esp.dmx.send(output_universe,
        ('\0\0\0'):rep(pos) .. '\xff\xff\xff' .. ('\0\0\0'):rep(leds - 1 - pos),
        network)
end

draw(0)

esp.dmx.on(control_universe, function(data)
    draw((data:byte(1, 1) * leds) >> 8)
end)
