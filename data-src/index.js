import { struct, u8, u16, u32, bool, cstr } from 'buffer-layout';

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

// Define the layout for app_config_t, matching the C struct
const AppConfigLayout = struct([
    cstr('sta_ssid', 33), // MAX_SSID_LEN + 1
    cstr('sta_password', 65), // MAX_PASSWORD_LEN + 1
    bool('sta_dhcp_enabled'),
    u32('sta_ip'),
    u8('sta_netmask_len'),
    u32('sta_gateway'),

    cstr('ap_ssid', 33), // MAX_SSID_LEN + 1
    cstr('ap_password', 65), // MAX_PASSWORD_LEN + 1
    u32('ap_ip'),
    u8('ap_netmask_len'),
    u32('ap_gateway'),

    u16('ble_interval'),
    u32('ble_duration_ms'),
    u8('ambitful_universe'),
    u16('ambitful_addr'),
    u8('ambitful_channels'),
    u8('ambitful_groups'),

    u8('dmx_in_universe'),
    u8('dmx_out_universe'),

    u8('ws2812_universe'),
]);

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
        const config = AppConfigLayout.decode(new Uint8Array(buffer));
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
            sta_dhcp_enabled: document.getElementById('sta_dhcp_enabled').checked,
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
        };

        const buffer = new Uint8Array(AppConfigLayout.span);
        AppConfigLayout.encode({ ...oldConfig, ...newConfig }, buffer);

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
