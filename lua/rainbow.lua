--- Draw rainbow
--

local leds = 50
local output_universe = 10
local network = false -- do not output to websocket

local hsv2rgb_rainbow = require('hsv2rgb').hsv2rgb_rainbow

local x = 0

esp.setInterval(function()
    local out = {}
    for i = 1, leds do
        table.insert(out, string.char(hsv2rgb_rainbow((x + i * 5) & 0xff, 255, 255)))
    end
    esp.dmx.send(output_universe, table.concat(out, ''), network)
    x = x + 1
end, 25)
