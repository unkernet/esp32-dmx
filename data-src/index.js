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


// Helper function to convert IP address from uint32 to string
function uint32ToIp(uint32) {
    return [
        (uint32 & 0xFF),
        (uint32 >> 8) & 0xFF,
        (uint32 >> 16) & 0xFF,
        (uint32 >> 24) & 0xFF,
    ].join('.');
}

// Helper function to convert IP address from string to uint32
function ipToUint32(ipString) {
    const parts = ipString.split('.').map(Number);
    if (parts.length !== 4 || parts.some(isNaN)) {
        return 0; // Return 0 or throw error for invalid IP
    }
    return (parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24)) >>> 0;
}

// Helper to parse "IP/CIDR" string
function parseIpCidr(ipCidrString) {
    const parts = ipCidrString.split('/');
    const ip = parts[0];
    const cidr = parseInt(parts[1], 10);
    return { ip: ipToUint32(ip), cidr: cidr };
}

// Helper to format IP and CIDR into "IP/CIDR" string
function formatIpCidr(ipUint32, cidrLen) {
    return `${uint32ToIp(ipUint32)}/${cidrLen}`;
}

const struct = [
  ['sta_ssid', 's33'],
  ['sta_password', 's65'],
  ['sta_dhcp_enabled', 'u8'],
  ['sta_ip', 'u32'],
  ['sta_netmask_len', 'u8'],
  ['sta_gateway', 'u32'],

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

const MOD_EN_DMX_IN      = (1<<0);
const MOD_EN_DMX_OUT     = (1<<1);
const MOD_EN_ARTNET_OUT  = (1<<2);
const MOD_EN_AMBITFUL    = (1<<3);
const MOD_EN_WS2812      = (1<<4);
const MOD_EN_ESPNOW      = (1<<5);
const MOD_EN_DMX_2_IN    = (1<<6);
const MOD_EN_DMX_2_OUT   = (1<<7);

const $ = document.getElementById.bind(document);

document.addEventListener('DOMContentLoaded', async function() {
    const form = $('configForm');
    let oldConfig;

    // Fetch configuration
    try {
        const response = await fetch('/config');
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const buffer = await response.arrayBuffer();
        const config = decodeStruct(buffer, struct);
        oldConfig = config;

        // Populate form fields
        $('sta_ssid').value = config.sta_ssid;
        $('sta_password').value = config.sta_password;
        $('sta_dhcp_enabled').checked = config.sta_dhcp_enabled;
        $('sta_ip_cidr').value = formatIpCidr(config.sta_ip, config.sta_netmask_len);
        $('sta_gateway').value = uint32ToIp(config.sta_gateway);

        $('ap_ssid').value = config.ap_ssid;
        $('ap_password').value = config.ap_password;

        $('en_artnet_out').checked = (config.enabled_modules & MOD_EN_ARTNET_OUT) > 0;

        $('en_ambitful').checked = (config.enabled_modules & MOD_EN_AMBITFUL) > 0;
        $('ambitful_universe').value = config.ambitful_universe;
        $('ambitful_addr').value = config.ambitful_addr + 1;
        $('ambitful_channel').value = config.ambitful_channel;
        $('ambitful_groups').value = config.ambitful_groups;

        $('en_dmx_in').checked = (config.enabled_modules & MOD_EN_DMX_IN) > 0;
        $('en_dmx_out').checked = (config.enabled_modules & MOD_EN_DMX_OUT) > 0;
        $('dmx_in_universe').value = config.dmx_in_universe;
        $('dmx_out_universe').value = config.dmx_out_universe;
        $('dmx_repeat_interval').value = (config.dmx_repeat_interval || 1) * 5;

        $('en_dmx_2_in').checked = (config.enabled_modules & MOD_EN_DMX_2_IN) > 0;
        $('en_dmx_2_out').checked = (config.enabled_modules & MOD_EN_DMX_2_OUT) > 0;
        $('dmx_2_in_universe').value = config.dmx_2_in_universe;
        $('dmx_2_out_universe').value = config.dmx_2_out_universe;
        $('dmx_2_repeat_interval').value = (config.dmx_2_repeat_interval || 1) * 5;

        $('en_ws2812').checked = (config.enabled_modules & MOD_EN_WS2812) > 0;
        $('ws2812_universe').value = config.ws2812_universe;

    } catch (error) {
        console.error('Error fetching config:', error);
    }

    // Handle form submission
    form.addEventListener('submit', async function(event) {
        event.preventDefault();

        const staIpCidr = parseIpCidr($('sta_ip_cidr').value);

        const newConfig = {
            sta_ssid: $('sta_ssid').value,
            sta_password: $('sta_password').value,
            sta_dhcp_enabled: $('sta_dhcp_enabled').checked ? 1 : 0,
            sta_ip: staIpCidr.ip,
            sta_netmask_len: staIpCidr.cidr,
            sta_gateway: ipToUint32($('sta_gateway').value),

            ap_ssid: $('ap_ssid').value,
            ap_password: $('ap_password').value,

            enabled_modules: ($('en_artnet_out').checked ? MOD_EN_ARTNET_OUT : 0) | 
              ($('en_ambitful').checked ? MOD_EN_AMBITFUL : 0) | 
              ($('en_dmx_in').checked ? MOD_EN_DMX_IN : 0) | 
              ($('en_dmx_out').checked ? MOD_EN_DMX_OUT : 0) | 
              ($('en_dmx_2_in').checked ? MOD_EN_DMX_2_IN : 0) | 
              ($('en_dmx_2_out').checked ? MOD_EN_DMX_2_OUT : 0) | 
              ($('en_ws2812').checked ? MOD_EN_WS2812 : 0),

            ambitful_universe: parseInt($('ambitful_universe').value, 10),
            ambitful_addr: parseInt($('ambitful_addr').value, 10) - 1,
            ambitful_channel: parseInt($('ambitful_channel').value, 10),
            ambitful_groups: parseInt($('ambitful_groups').value, 10),

            dmx_in_universe: parseInt($('dmx_in_universe').value, 10),
            dmx_out_universe: parseInt($('dmx_out_universe').value, 10),
            dmx_repeat_interval: (parseInt($('dmx_repeat_interval').value, 10) / 5) | 0,

            dmx_2_in_universe: parseInt($('dmx_2_in_universe').value, 10),
            dmx_2_out_universe: parseInt($('dmx_2_out_universe').value, 10),
            dmx_2_repeat_interval: (parseInt($('dmx_2_repeat_interval').value, 10) / 5) | 0,

            ws2812_universe: parseInt($('ws2812_universe').value, 10),
          };

        const buffer = encodeStruct({ ...oldConfig, ...newConfig }, struct);

        try {
            const response = await fetch('/config', {
                method: 'PUT',
                headers: {
                    'Content-Type': 'application/octet-stream',
                },
                body: buffer,
                signal: AbortSignal.timeout(3000),
            });

            if (response.ok) {
                alert('Configuration saved and device restarting!');
            } else {
                const errorText = await response.text();
                alert(`Failed to save configuration: ${errorText}`);
            }
        } catch (error) {
            console.error('Error saving config:', error);
            alert('Error saving configuration.');
        }
    });
});
