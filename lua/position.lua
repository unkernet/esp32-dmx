--- Turn on single led, controlled by channel 1 of another Universe
--

local leds = 50
local control_universe = 19
local output_universe = 20
local network = false -- do not output to websocket

local pos = 0

while true do
    dmx.send(output_universe,  ('\0\0\0'):rep(pos) .. '\xff\xff\xff' .. ('\0\0\0'):rep(leds - 1 - pos), network)
    local read = dmx.read(control_universe, 5000)
    if read then
        pos = (read:byte(1, 1) * leds) >> 8
    end
end
