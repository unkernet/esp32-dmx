--- This is Lua port of FastLED hsv2rgb library
-- 

local hsv2rgb = {}

local HSV_SECTION_3 = 0x40

local function scale8(i, scale)
  return ((i * scale) >> 8) & 0xFF
end

local function scale8_video(i, scale)
  if i + scale == 0 then
    return 0
  end
  local j = scale8(i, scale)
  if j == 0 then
    return 1
  end
  return j
end

function hsv2rgb.hsv2rgb(h, s, v)
  local invsat = (255 - s) & 0xFF
  local brightness = (v * invsat) >> 8

  local amplitude = (v - brightness) & 0xFF

  h = scale8(h, 191)
  local section = h // HSV_SECTION_3
  local offset = h % HSV_SECTION_3

  local rampup = ((offset * amplitude) // 64 + brightness) & 0xFF
  local rampdown = (((HSV_SECTION_3 - 1) - offset * amplitude) // 64 + brightness) & 0xFF

  if section ~= 0 then
    if section == 1 then
      return brightness, rampdown, rampup
    else
      return rampup, brightness, rampdown
    end
  else
    return rampdown, rampup, brightness
  end
end

function hsv2rgb.hsv2rgb_rainbow(hue, sat, val)
  local offset8 = (hue & 0x1F) << 3
  local third = scale8(offset8, 85) -- 85 is approx 256/3

  local r, g, b

  if (hue & 0x80) == 0 then
    if (hue & 0x40) == 0 then
      if (hue & 0x20) == 0 then
        r, g, b = (255 - third) & 0xFF, third, 0
      else
        -- Y1 boost (default)
        r, g, b = 171, (85 + third) & 0xFF, 0
      end
    else
      if (hue & 0x20) == 0 then
        -- Y1 boost
        local twothirds = scale8(offset8, 170)
        r, g, b = (171 - twothirds) & 0xFF, (170 + third) & 0xFF, 0
      else
        r, g, b = 0, (255 - third) & 0xFF, third
      end
    end
  else
    if (hue & 0x40) == 0 then
      if (hue & 0x20) == 0 then
        local twothirds = scale8(offset8, 170)
        r, g, b = 0, (171 - twothirds) & 0xFF, (85 + twothirds) & 0xFF
      else
        r, g, b = third, 0, (255 - third) & 0xFF
      end
    else
      if (hue & 0x20) == 0 then
        r, g, b = (85 + third) & 0xFF, 0, (171 - third) & 0xFF
      else
        r, g, b = (170 + third) & 0xFF, 0, (85 - third) & 0xFF
      end
    end
  end

  if sat ~= 255 then
    if sat == 0 then
      r, g, b = 255, 255, 255
    else
      local desat = (255 - sat) & 0xFF
      desat = scale8_video(desat, desat)
      local satscale = (255 - desat) & 0xFF
      if r ~= 0 then r = (scale8(r, satscale) + 1) & 0xFF end
      if g ~= 0 then g = (scale8(g, satscale) + 1) & 0xFF end
      if b ~= 0 then b = (scale8(b, satscale) + 1) & 0xFF end
      r, g, b = (r + desat) & 0xFF, (g + desat) & 0xFF, (b + desat) & 0xFF
    end
  end

  if val ~= 255 then
    val = scale8_video(val, val)
    if val == 0 then
      r, g, b = 0, 0, 0
    else
      if r ~= 0 then r = (scale8(r, val) + 1) & 0xFF end
      if g ~= 0 then g = (scale8(g, val) + 1) & 0xFF end
      if b ~= 0 then b = (scale8(b, val) + 1) & 0xFF end
    end
  end

  return r, g, b
end

return hsv2rgb
