import { decodeStruct, encodeStruct, sizeofStruct } from './struct.js';

const enabledModules = [
  'dmx_0_in', 'dmx_0_out',
  'dmx_1_in', 'dmx_1_out',
  'dmx_2_in', 'dmx_2_out',
  'dmx_3_in', 'dmx_3_out',
  'ws2812_0', 'ws2812_1', 'ws2812_2', 'ws2812_3',
  'artnet_out', 'artnet_in', 'artnet_ws', 'reserved_15',
  'lua',
  'ambitful',
  'mqtt',
];

const dmxPortSchema = [
    ['in_universe', 'u16'],
    ['out_universe', 'u16'],
    ['repeat_interval', 'u16'],
    ['repeat_time', 'u8'],
];

const ws2812PortSchema = [
    ['universe', 'u16'],
];

const ambitfulSchema = [
    ['universe', 'u16'],
    ['addr', 'u16'],
    ['channel', 'u8'],
    ['groups', 'u8'],
];

const wifiSchema = [
  ['sta', [
    ['ssid', 's33'],
    ['password', 's65'],
    ['dhcp_enabled', 'u8'],
    ['ip', 'u32'],
    ['netmask_len', 'u8'],
  ]],
  ['ap', [
    ['ssid', 's33'],
    ['password', 's65'],
  ]],
];

const schema = [
  ['magic', 'u16'],
  ['version', 'u16'],
  ['wifi', wifiSchema],
  ['enabled_modules', 'u32', enabledModules],
  ['dmx_ports', [4, dmxPortSchema]],
  ['ws2812_ports', [4, ws2812PortSchema]],
  ['ambitful', ambitfulSchema],
  ['mqtt_broker_uri', 's256'],
  ['reserved', 's28'],
];

const metaSchema = [
  ['supported', 'u32', enabledModules],
  ['dev_name', 's5'],
  ['dmx_name', [4, 's16']],
];

const apRecord = [
  -1, [
    ['bssid', 's6'],
    ['ssid', 's33'],
    ['channel', 'u8'],
    ['rssi', 'i8'],
    ['authmode', 'u8'],
  ]
];

const scriptList = [
  ['state', 'u8'],
  ['running', 's64'],
  ['error', 's128'],
  ['files', [-1, 's48']],
]

export const mqttStatus = [
    'Disabled',
    'Connecting',
    'Connected',
    'Disconnected',
];

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

export function parseMeta(buffer) {
  return decodeStruct(buffer, metaSchema);
}

export function parseConfig(buffer) {
  const size = sizeofStruct(schema);
  const obj = decodeStruct(buffer, schema);
  obj.meta = parseMeta(buffer.slice(size));
  
  // Transform binary to high-level strings
  obj.wifi.sta.ip_cidr = formatIpCidr(obj.wifi.sta.ip, obj.wifi.sta.netmask_len);
  delete obj.wifi.sta.ip;
  delete obj.wifi.sta.netmask_len;

  obj.dmx_ports.forEach(p => {
    p.repeat_time_endless = p.repeat_time == 0xff;
  });

  return obj;
}

export function serializeConfig(conf) {
    const data = JSON.parse(JSON.stringify(conf));
    
    // Transform high-level strings back to binary
    const { ip, cidr } = parseIpCidr(conf.wifi.sta.ip_cidr);
    data.wifi.sta.ip = ip;
    data.wifi.sta.netmask_len = cidr;
    delete data.wifi.sta.ip_cidr;

    data.dmx_ports.forEach(p => {
        if (p.repeat_time_endless) {
            p.repeat_time = 255;
        }
    });

    return encodeStruct(data, schema);
}

export function parseApRecords(data) {
  return decodeStruct(data, apRecord);
}

export function parseScriptList(data) {
  return decodeStruct(data, scriptList);
}

/* For mock only */
export function serializeMeta(info) { 
  return encodeStruct(info, metaSchema);
}
export function serializeAp(data) { 
  return encodeStruct(data, apRecord);
}
export function serializeScriptList(data) {
  return encodeStruct(data, scriptList);
}
