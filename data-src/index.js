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
  ['ap_ip', 'u32'],
  ['ap_netmask_len', 'u8'],
  ['ap_gateway', 'u32'],

  ['ble_interval', 'u16'],
  ['ble_duration_ms', 'u32'],
  ['ambitful_universe', 'u8'],
  ['ambitful_addr', 'u16'],
  ['ambitful_channel', 'u8'],
  ['ambitful_groups', 'u8'],

  ['dmx_in_universe', 'u8'],
  ['dmx_out_universe', 'u8'],

  ['ws2812_universe', 'u8']
];

document.addEventListener('DOMContentLoaded', async function() {
    const form = document.getElementById('configForm');
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
        document.getElementById('sta_ssid').value = config.sta_ssid;
        document.getElementById('sta_password').value = config.sta_password;
        document.getElementById('sta_dhcp_enabled').checked = config.sta_dhcp_enabled;
        document.getElementById('sta_ip_cidr').value = formatIpCidr(config.sta_ip, config.sta_netmask_len);
        document.getElementById('sta_gateway').value = uint32ToIp(config.sta_gateway);

        document.getElementById('ap_ssid').value = config.ap_ssid;
        document.getElementById('ap_password').value = config.ap_password;
        document.getElementById('ap_ip_cidr').value = formatIpCidr(config.ap_ip, config.ap_netmask_len);
        document.getElementById('ap_gateway').value = uint32ToIp(config.ap_gateway);

        document.getElementById('ble_interval').value = config.ble_interval;
        document.getElementById('ble_duration_ms').value = config.ble_duration_ms;
        document.getElementById('ambitful_universe').value = config.ambitful_universe;
        document.getElementById('ambitful_addr').value = config.ambitful_addr;
        document.getElementById('ambitful_channel').value = config.ambitful_channel;
        document.getElementById('ambitful_groups').value = config.ambitful_groups;

    } catch (error) {
        console.error('Error fetching config:', error);
    }

    // Handle form submission
    form.addEventListener('submit', async function(event) {
        event.preventDefault();

        const staIpCidr = parseIpCidr(document.getElementById('sta_ip_cidr').value);
        const apIpCidr = parseIpCidr(document.getElementById('ap_ip_cidr').value);

        const newConfig = {
            sta_ssid: document.getElementById('sta_ssid').value,
            sta_password: document.getElementById('sta_password').value,
            sta_dhcp_enabled: document.getElementById('sta_dhcp_enabled').checked ? 1 : 0,
            sta_ip: staIpCidr.ip,
            sta_netmask_len: staIpCidr.cidr,
            sta_gateway: ipToUint32(document.getElementById('sta_gateway').value),

            ap_ssid: document.getElementById('ap_ssid').value,
            ap_password: document.getElementById('ap_password').value,
            ap_ip: apIpCidr.ip,
            ap_netmask_len: apIpCidr.cidr,
            ap_gateway: ipToUint32(document.getElementById('ap_gateway').value),

            ble_interval: parseInt(document.getElementById('ble_interval').value, 10),
            ble_duration_ms: parseInt(document.getElementById('ble_duration_ms').value, 10),
            ambitful_universe: parseInt(document.getElementById('ambitful_universe').value, 10),
            ambitful_addr: parseInt(document.getElementById('ambitful_addr').value, 10),
            ambitful_channel: parseInt(document.getElementById('ambitful_channel').value, 10),
            ambitful_groups: parseInt(document.getElementById('ambitful_groups').value, 10),
        };

        const buffer = encodeStruct({ ...oldConfig, ...newConfig }, struct);

        try {
            const response = await fetch('/config', {
                method: 'PUT',
                headers: {
                    'Content-Type': 'application/octet-stream',
                },
                body: buffer,
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
