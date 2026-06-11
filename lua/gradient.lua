--- Draw gradient controlled by another dmx Universe
--

local leds = 50
local control_universe = 1
local output_universe = 10
local network = false -- do not output to websocket

local hsv2rgb_rainbow = require('hsv2rgb').hsv2rgb_rainbow

local ar, ag, ab = hsv2rgb_rainbow(0, 255, 255)
local br, bg, bb = hsv2rgb_rainbow(127, 255, 255)
leds = leds - 1

local function draw()
  local out = {}
  for i = 0, leds do
    table.insert(out, string.char(
      math.floor((ar * (leds - i) / leds) + (br * i / leds)),
      math.floor((ag * (leds - i) / leds) + (bg * i / leds)),
      math.floor((ab * (leds - i) / leds) + (bb * i / leds))
    ))
  end
  esp.dmx.send(output_universe, table.concat(out, ''), network)
end

-- Update endpoints from channels 1..6 of control Universe
esp.dmx.on(control_universe, function(data)
  if #data > 5 then
    ar, ag, ab = hsv2rgb_rainbow(string.byte(data, 1, 3))
    br, bg, bb = hsv2rgb_rainbow(string.byte(data, 4, 6))
    draw()
  end
end)

draw()
