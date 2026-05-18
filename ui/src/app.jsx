import { WifiTab } from "./components/WifiTab";
import { ScriptingTab } from "./components/ScriptingTab";
import { ConfigTabs } from "./components/ConfigTabs";
import { MonitorTab } from "./components/MonitorTab";
import { ControlTab } from "./components/ControlTab";
import { SystemTab } from "./components/SystemTab";
import { API } from "./api";
import { config, isDirty, activeTab } from "./signals";
import { useSignalEffect } from '@preact/signals';
import { useMemo, useState } from 'preact/hooks';
import { Modal } from "./components/Common";
import { 
  Wifi, 
  Bluetooth, 
  Monitor as MonitorIcon, 
  Cpu, 
  RefreshCcw,
  SlidersVertical,
  // Network as ArtnetIcon,
} from "lucide-preact";
import ArtnetIcon from '../img/artnet.svg?react';
import DmxIcon from '../img/dmx.svg?react';
import Ws2812Icon from '../img/ws2812.svg?react';
import ScriptIcon from '../img/script.svg?react';

const tabs = [
  { id: "wifi", icon: Wifi, label: "WiFi", component: WifiTab }, 
  { id: "artnet", icon: ArtnetIcon, label: "Art-Net", component: () => <ConfigTabs moduleName="Art-Net" /> },
  { id: "dmx", icon: DmxIcon, label: "DMX", component: () => <ConfigTabs moduleName="DMX" /> },
  { id: "ble", icon: Bluetooth, label: "Ambitful", component: () => <ConfigTabs moduleName="Ambitful BLE" /> },
  { id: "ws2812", icon: Ws2812Icon, label: "Led", component: () => <ConfigTabs moduleName="WS2812" />, className: 'bigger' },
  { id: "scripting", icon: ScriptIcon, label: "Scripting", component: ScriptingTab },
  { id: "monitor", icon: MonitorIcon, label: "Monitor", component: MonitorTab },
  { id: "control", icon: SlidersVertical, label: "Control", component: ControlTab },
  { id: "system", icon: Cpu, label: "System", component: SystemTab },
];

function isEqual(a, b) {
  if (a === b) return true;
  if (!a || !b) return false;
  const keysA = Object.keys(a);
  if (keysA.length !== Object.keys(b).length) return false;
  return keysA.every(key => {
    if (typeof(a[key]) === 'object' && typeof(a[key]) === typeof(b[key])) {
      return isEqual(a[key], b[key])
    }
    return a[key] === b[key];
  });
}

export function App() {
  const [initialConfig, setInitialConfig] = useState(null);
  const [showRebootConfirm, setShowRebootConfirm] = useState(false);
  const [waitReboot, setWaitReboot] = useState(false);

  const { supported } = config.value?.meta || { supported: 0 };

  // Filter tabs based on supported modules
  const filteredTabs = useMemo(() => {
    const supportedMap = {
      artnet: ['artnet_out', 'artnet_in'],
      dmx: ['dmx_0_in', 'dmx_0_out', 'dmx_1_in', 'dmx_1_out', 'dmx_2_in', 'dmx_2_out', 'dmx_3_in', 'dmx_3_out'],
      ble: ['ambitful'],
      ws2812: ['ws2812_0', 'ws2812_1', 'ws2812_2', 'ws2812_3'],
      scripting: ['lua'],
    };

    return tabs.filter(tab => {
      const flags = supportedMap[tab.id];
      return !flags || flags.some(flag => supported[flag]);
    });
  }, [supported]);

  // Load initial config
  useSignalEffect(() => {
    API.getConfig().then(c => {
      config.value = c;
      setInitialConfig({ ...c });
    }).catch(e => {
      console.error("Failed to load config:", e);
      alert("Failed to load device configuration.");
    });
  });

  // Check if config is dirty
  useSignalEffect(() => {
    if (config.value && initialConfig) {
      isDirty.value = !isEqual(config.value, initialConfig);
    }
  });

  const handleSaveAndRestart = async () => {
    try {
      setWaitReboot(true);
      await API.saveConfig(config.value);
      setTimeout(() => {
        location.reload();
      }, 1000);
    } catch (e) {
      setWaitReboot(false);
      alert("Failed to save config: " + e.message);
    }
  };

  const CurrentTabComponent = useMemo(() => {
    const tab = filteredTabs.find(t => t.id === activeTab.value);
    return tab ? tab.component : () => <p>Tab not found</p>;
  }, [activeTab.value, filteredTabs]);

  return (
    <div class="layout">
      <nav class="sidebar">
        <ul>
          {filteredTabs.map(tab => (
            <li key={tab.id} class={tab.className}>
              <a 
                href="#" 
                class={activeTab.value === tab.id ? "" : "secondary"}
                onClick={(e) => { activeTab.value = tab.id; e.preventDefault(); }}
                title={tab.label}
              >
                { typeof (tab.icon) === 'string' ? <span dangerouslySetInnerHTML={tab.icon} /> : <tab.icon size={24} />}
              </a>
            </li>
          ))}
        </ul>
      </nav>

      <main class={"container" + (isDirty.value ? " dirty" : "")}>
        <h1>{filteredTabs.find(t => t.id === activeTab.value)?.label}</h1>
        <CurrentTabComponent />
      </main>

      {isDirty.value && (
        <footer class="save-bar">
          <div class="container">
            <button class="primary" onClick={() => setShowRebootConfirm(true)}>
              <RefreshCcw size={20} />
              Save & Restart
            </button>
          </div>
        </footer>
      )}

      <Modal
        isOpen={showRebootConfirm}
        title="Save configuration"
        onClose={() => setShowRebootConfirm(false)}
        onConfirm={handleSaveAndRestart}
        confirmText="Save"
        busy={waitReboot}
      >
        <p>Save configuration and reboot device?</p>
      </Modal>
    </div>
  );
}
