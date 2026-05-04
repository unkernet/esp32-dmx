local leds = 50
local pos = 0

while 1 do
  local data = string.rep('\x00', pos * 3) .. string.rep('\x0f', 3) .. string.rep('\x00', (leds - pos - 1) * 3)
  dmx_send(20, data)

  local read = dmx_read(19, 20)
  if read and #read > 0 then
    local p = string.byte(read, 1, 1)
    pos = p
  else
    pos = math.fmod(random(), leds)
  end
  if pos >= leds then pos = 0 end
end
