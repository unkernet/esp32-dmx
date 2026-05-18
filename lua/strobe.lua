--- Strobe with controlled frequency
--

local leds = 10
local control_universe = 1
local output_universe = 10

local delay = 150
local state = 0
while true do
  dmx.send(output_universe, string.char(state):rep(leds * 3))
  if state == 0 then
    state = 100
  else
    state = 0
  end
  local t = os.clock()
  local data = dmx.read(control_universe, delay)
  if data then
    delay = string.byte(data, 1, 1) + 20
    t = math.floor(delay - (os.clock() - t) * 1000)
    if t > 1 then
      -- sleep the rest of delay time
      sleep(t)
    end

  end
end
