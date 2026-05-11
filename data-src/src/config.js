export const struct = [
  ['sta_ssid', 's33'],
  ['sta_password', 's65'],
  ['sta_dhcp_enabled', 'u8'],
  ['sta_ip', 'u32'],
  ['sta_netmask_len', 'u8'],

  ['dmx_repeat_time', 'u8'],
  ['dmx_2_repeat_time', 'u8'],
  ['reserved_0', 's2'],

  ['ap_ssid', 's33'],
  ['ap_password', 's65'],

  ['enabled_modules', 'u8'],

  ['ambitful_universe', 'u16'],
  ['ambitful_addr', 'u16'],
  ['ambitful_channel', 'u8'],
  ['ambitful_groups', 'u8'],

  ['dmx_in_universe', 'u16'],
  ['dmx_out_universe', 'u16'],

  ['ws2812_universe', 'u16'],

  ['dmx_repeat_interval', 'u8'],

  ['dmx_2_in_universe', 'u16'],
  ['dmx_2_out_universe', 'u16'],
  ['dmx_2_repeat_interval', 'u8'],

  ['reserved_1', 's4'],
];

export const enabledModules = {
  dmx_in    : (1<<0),
  dmx_out   : (1<<1),
  artnet_out: (1<<2),
  ambitful  : (1<<3),
  ws2812    : (1<<4),
  espnow    : (1<<5),
  dmx_2_in  : (1<<6),
  dmx_2_out : (1<<7),
};

function structSize(schema) {
  let size = 0;
  for (const [_, type] of schema) {
    if (type === 'u8') size += 1;
    else if (type === 'u16') size += 2;
    else if (type === 'u32') size += 4;
    else if (type[0] === 's') size += parseInt(type.slice(1), 10);
    else throw new Error('unknown type: ' + type);
  }
  return size;
}

export function decodeStruct(buffer, schema) {
  const view = new DataView(buffer);
  const bytes = new Uint8Array(buffer);
  let offset = 0;
  const out = {};

  for (const [name, type] of schema) {
    if (type === 'u8') {
      out[name] = view.getUint8(offset);
      offset += 1;
    } else if (type === 'u16') {
      out[name] = view.getUint16(offset, true);
      offset += 2;
    } else if (type === 'u32') {
      out[name] = view.getUint32(offset, true);
      offset += 4;
    } else if (type[0] === 's') {
      const len = parseInt(type.slice(1), 10);
      const slice = bytes.subarray(offset, offset + len);
      const end = slice.indexOf(0);
      const realEnd = end >= 0 ? end : len;
      out[name] = new TextDecoder().decode(slice.subarray(0, realEnd));
      offset += len;
    } else {
      throw new Error('unknown type: ' + type);
    }
  }

  return out;
}

export function encodeStruct(obj, schema) {
  const size = structSize(schema);
  const buffer = new ArrayBuffer(size);
  const view = new DataView(buffer);
  const bytes = new Uint8Array(buffer);
  let offset = 0;

  for (const [name, type] of schema) {
    const value = obj[name];

    if (type === 'u8') {
      view.setUint8(offset, value);
      offset += 1;
    } else if (type === 'u16') {
      view.setUint16(offset, value, true);
      offset += 2;
    } else if (type === 'u32') {
      view.setUint32(offset, value, true);
      offset += 4;
    } else if (type[0] === 's') {
      const len = parseInt(type.slice(1), 10);
      const enc = new TextEncoder().encode(value || '');
      const n = Math.min(enc.length, len - 1);
      bytes.set(enc.subarray(0, n), offset);
      bytes[offset + n] = 0;    // null-termination
      for (let i = offset + n + 1; i < offset + len; i++) bytes[i] = 0;
      offset += len;
    } else {
      throw new Error('unknown type: ' + type);
    }
  }

  return buffer;
}

// IP Helpers
export function uint32ToIp(uint32) {
    return [
        (uint32 & 0xFF),
        (uint32 >> 8) & 0xFF,
        (uint32 >> 16) & 0xFF,
        (uint32 >> 24) & 0xFF,
    ].join('.');
}

export function ipToUint32(ipString) {
    const parts = ipString.split('.').map(Number);
    if (parts.length !== 4 || parts.some(isNaN)) {
        return 0;
    }
    return (parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24)) >>> 0;
}

export function parseIpCidr(ipCidrString) {
    const parts = ipCidrString.split('/');
    const ip = parts[0];
    const cidr = parseInt(parts[1], 10);
    return { ip: ipToUint32(ip), cidr: cidr };
}

export function formatIpCidr(ipUint32, cidrLen) {
    return `${uint32ToIp(ipUint32)}/${cidrLen}`;
}

export function parseConfig(buffer) {
  const obj = decodeStruct(buffer, struct);

  
  // Transform binary to high-level strings
  obj.sta_ip_cidr = formatIpCidr(obj.sta_ip, obj.sta_netmask_len);
  const enabled_modules = {};
  for (const mod in enabledModules) {
    enabled_modules[mod] = (obj.enabled_modules & enabledModules[mod]) > 0;
  }
  obj.enabled_modules = enabled_modules;
  delete obj.sta_ip;
  delete obj.sta_netmask_len;

  obj.dmx_repeat_interval *= 5;
  obj.dmx_2_repeat_interval *= 5;
  obj.dmx_repeat_time_endless = obj.dmx_repeat_time == 0xff;
  obj.dmx_2_repeat_time_endless = obj.dmx_2_repeat_time == 0xff;

  return obj;
}

export function serializeConfig(conf) {
    const data = { ...conf };
    
    // Transform high-level strings back to binary
    const { ip, cidr } = parseIpCidr(conf.sta_ip_cidr);
    data.sta_ip = ip;
    data.sta_netmask_len = cidr;
    delete data.sta_ip_cidr;

    data.dmx_repeat_interval = (data.dmx_repeat_interval / 5) | 0;
    data.dmx_2_repeat_interval = (data.dmx_2_repeat_interval / 5) | 0;
    if (data.dmx_repeat_time_endless) {
      data.dmx_repeat_time = 255;
    }
    if (data.dmx_2_repeat_time_endless) {
      data.dmx_2_repeat_time = 255;
    }
    let enabled_modules = 0;
    for (const mod in enabledModules) {
      if (data.enabled_modules[mod]) {
        enabled_modules |= enabledModules[mod];
      }
    }
    data.enabled_modules = enabled_modules;

    return encodeStruct(data, struct);
}
