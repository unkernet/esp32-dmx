const struct = [
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

const metaStruct = [
  ['supported', 'u8'],
  ['dev_name', 's5'],
  ['dmx_name', 's16'],
  ['dmx_2_name', 's16'],
];

const enabledModules = [
  'dmx_in', 'dmx_out',
  'artnet_out',
  'ambitful',
  'ws2812',
  'lua',
  'dmx_2_in', 'dmx_2_out',
];

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

function decodeStruct(buffer, schema) {
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

function encodeStruct(obj, schema) {
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
function uint32ToIp(uint32) {
    return [
        (uint32 & 0xFF),
        (uint32 >> 8) & 0xFF,
        (uint32 >> 16) & 0xFF,
        (uint32 >> 24) & 0xFF,
    ].join('.');
}

function ipToUint32(ipString) {
    const parts = ipString.split('.').map(Number);
    if (parts.length !== 4 || parts.some(isNaN)) {
        return 0;
    }
    return (parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24)) >>> 0;
}

function parseIpCidr(ipCidrString) {
    const parts = ipCidrString.split('/');
    const ip = parts[0];
    const cidr = parseInt(parts[1], 10);
    return { ip: ipToUint32(ip), cidr: cidr };
}

function formatIpCidr(ipUint32, cidrLen) {
    return `${uint32ToIp(ipUint32)}/${cidrLen}`;
}

function parseBitfield(value, fields) {
  const ret = {};
  fields.forEach((key, i) => {
    ret[key] = (value & (1 << i)) > 0;
  });
  return ret;
}

function serializeBitfield(value, fields) {
  let ret = 0;
  fields.forEach((key, i) => {
    if (value[key]) {
      ret |= (1 << i);
    }
  });
  return ret;
}

export function parseConfig(buffer) {
  const size = structSize(struct);
  const obj = decodeStruct(buffer.slice(0, size), struct);
  obj.meta = decodeStruct(buffer.slice(size), metaStruct);
  obj.meta.supported = parseBitfield(obj.meta.supported, enabledModules);

  // Transform binary to high-level strings
  obj.sta_ip_cidr = formatIpCidr(obj.sta_ip, obj.sta_netmask_len);
  obj.enabled_modules = parseBitfield(obj.enabled_modules, enabledModules);
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
    data.enabled_modules = serializeBitfield(data.enabled_modules, enabledModules);

    return encodeStruct(data, struct);
}

export function serializeMeta(info) { // For mock only
  info.supported = serializeBitfield(info.supported, enabledModules);
  return encodeStruct(info, metaStruct);
}
