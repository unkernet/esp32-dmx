import { useState } from 'preact/hooks';
import { signal, computed } from "@preact/signals";
import { config } from "../signals";
import { API } from "../api";
import { Card, FormField } from "./Common";
import { RefreshCw, Search, Edit2 } from "lucide-preact";
import { updateConfig } from '../util';

export function WifiTab() {
  const [scanning, setScanning] = useState(false);
  const [scanResults, setScanResults] = useState([]);
  const [isManual, setIsManual] = useState(true);

  if (!config.value) return <p>Loading configuration...</p>;

  const handleScan = async () => {
    setScanning(true);
    try {
      const results = (await API.scanWifi()).filter(net => net.ssid);
      results.sort((a, b) => b.rssi - a.rssi);
      setScanResults(results);
      setIsManual(false);
    } catch (e) {
      alert("Scan failed: " + e.message);
    } finally {
      setScanning(false);
    }
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
                value={configValue.wifi.sta.ssid}
                onInput={(e) => updateConfig('wifi.sta.ssid', e.target.value)}
              />
            ) : (
              <select onInput={(e) => updateConfig('wifi.sta.ssid', e.target.value)} value={configValue.wifi.sta.ssid}>
                <option disabled value="">Select network</option>
                {scanResults.map(net => (
                  <option value={net.ssid}>{net.ssid} {net.authmode ? '' : '🔓'} ({net.rssi}dBm)</option>
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

        <FormField label="Password">
          <input 
            type="text" 
            placeholder="Network password"
            value={configValue.wifi.sta.password}
            onInput={(e) => updateConfig('wifi.sta.password', e.target.value)}
          />
        </FormField>

        <FormField label="Enable DHCP">
          <input 
            type="checkbox" 
            role="switch"
            checked={configValue.wifi.sta.dhcp_enabled}
            onInput={(e) => updateConfig('wifi.sta.dhcp_enabled', e.target.checked ? 1 : 0)}
          />
        </FormField>

        { configValue.wifi.sta.dhcp_enabled ? null : <>
          <FormField label="IP address">
            <input 
              type="text" 
              value={configValue.wifi.sta.ip_cidr}
              onInput={(e) => updateConfig('wifi.sta.ip_cidr', e.target.value)}
            />
          </FormField>
        </>}
      </Card>

      <Card title="Access Point Settings" notice="The internal network hosted by this device.">
        <FormField label="AP SSID" description="SSID for the device's own hotspot.">
          <input 
            type="text" 
            value={configValue.wifi.ap.ssid}
            onInput={(e) => updateConfig('wifi.ap.ssid', e.target.value)}
          />
        </FormField>
        <FormField label="AP Password">
          <input 
            type="text" 
            value={configValue.wifi.ap.password}
            onInput={(e) => updateConfig('wifi.ap.password', e.target.value)}
          />
        </FormField>
      </Card>
    </div>
  );
}
