local data = {}
local leds = 50
local r = 0
local a = 1

while 1 do
  for i = 0, leds - 1 do
    data[i*3+1] = r
    data[i*3+2] = 0
    data[i*3+3] = 255-r
  end
  r=r+a
  if r == 255 then a = -1 end
  if r == 0 then a = 1 end
  send_dmx(20, data)
  sleep(30)
end
