import { useState } from 'preact/hooks';
import { signal, computed } from "@preact/signals";
import { config } from "../signals";
import { API } from "../api";
import { Card, FormField } from "./Common";
import { RefreshCw, Search, Edit2 } from "lucide-preact";

export function WifiTab() {
  const [scanning, setScanning] = useState(false);
  const [scanResults, setScanResults] = useState([]);
  const [isManual, setIsManual] = useState(true);

  if (!config.value) return <p>Loading configuration...</p>;

  const handleScan = async () => {
    setScanning(true);
    try {
      const results = await API.scanWifi();
      results.sort((a, b) => b.rssi - a.rssi);
      setScanResults(results);
      setIsManual(false);
    } catch (e) {
      alert("Scan failed: " + e.message);
    } finally {
      setScanning(false);
    }
  };

  const updateConfig = (key, val) => {
    config.value = { ...config.value, [key]: val }
  };

  const { value: configValue } = config;

    return (
    <div class="grid">
      <Card title="Station Settings" notice="Configure how the device connects to your local network.">
        <FormField 
          label="SSID" 
          description="The name of your WiFi network."
        >
          <div role="group">
            {isManual ? (
              <input 
                type="text" 
                placeholder="Enter SSID" 
                value={configValue.sta_ssid}
                onInput={(e) => updateConfig('sta_ssid', e.target.value)}
              />
            ) : (
              <select onInput={(e) => updateConfig('sta_ssid', e.target.value)} value={configValue.sta_ssid}>
                {scanResults.map(net => (
                  <option value={net.ssid}>{net.ssid} ({net.rssi}dBm)</option>
                ))}
              </select>
            )}
            <button 
              class="secondary outline scan-btn" 
              onClick={() => isManual ? handleScan() : setIsManual(true)}
              aria-busy={scanning}
            >
              {(scanning) ? null : (isManual ? <Search size={20} /> : <Edit2 size={20} />)}
            </button>
          </div>
        </FormField>

        <FormField label="Password" description="WiFi password.">
          <input 
            type="text" 
            placeholder="Network password"
            value={configValue.sta_password}
            onInput={(e) => updateConfig('sta_password', e.target.value)}
          />
        </FormField>

        <label>
          <input 
            type="checkbox" 
            role="switch"
            checked={configValue.sta_dhcp_enabled}
            onInput={(e) => updateConfig('sta_dhcp_enabled', e.target.checked ? 1 : 0)}
          />
          Enable DHCP
        </label>

        { configValue.sta_dhcp_enabled ? null : <>
          <FormField label="IP address">
            <input 
              type="text" 
              value={configValue.sta_ip_cidr}
              onInput={(e) => updateConfig('sta_ip_cidr', e.target.value)}
            />
          </FormField>
        </>}
      </Card>

      <Card title="Access Point Settings" notice="The internal network hosted by this device.">
        <FormField label="AP SSID" description="SSID for the device's own hotspot.">
          <input 
            type="text" 
            value={configValue.ap_ssid}
            onInput={(e) => updateConfig('ap_ssid', e.target.value)}
          />
        </FormField>
        <FormField label="AP Password" description="Min 8 characters.">
          <input 
            type="text" 
            value={configValue.ap_password}
            onInput={(e) => updateConfig('ap_password', e.target.value)}
          />
        </FormField>
      </Card>
    </div>
  );
}
