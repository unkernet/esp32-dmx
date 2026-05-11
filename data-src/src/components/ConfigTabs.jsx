import { Card, FormField } from "./Common";
import { config } from "../signals";
import { useState } from 'preact/hooks';

export function ConfigTabs({ moduleName }) {
  const { value: configValue } = config;
  if (!configValue) return <p>Loading configuration...</p>;

  const updateConfig = (key, val) => {
    config.value = { ...config.value, [key]: val };
  };

  const updateConfigMod = (key, val) => {
    config.value = { ...config.value, enabled_modules: { ...configValue.enabled_modules, [key]: val } };
  };

  const renderDmxConfig = (modulePrefix, labelPrefix) => {
    const endless = configValue[`${modulePrefix}_repeat_time_endless`];
    return <Card title={`${labelPrefix} Configuration`}>
      <label>
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules[`${modulePrefix}_in`]}
          onInput={(e) => updateConfigMod(`${modulePrefix}_in`, e.target.checked)} 
        />
        Enable Input
      </label>
      <FormField label={`Input Universe`} description="Assign incoming DMX data to this universe.">
        <input 
          type="number" 
          min="0" max="32767" 
          value={configValue[`${modulePrefix}_in_universe`]}
          onInput={(e) => updateConfig(`${modulePrefix}_in_universe`, parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <label>
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules[`${modulePrefix}_out`]}
          onInput={(e) => updateConfigMod(`${modulePrefix}_out`, e.target.checked)} 
        />
        Enable Output
      </label>
      <FormField label={`Output Universe`} description="Output data from this universe to the DMX port.">
        <input 
          type="number" 
          min="0" max="32767" 
          value={configValue[`${modulePrefix}_out_universe`]}
          onInput={(e) => updateConfig(`${modulePrefix}_out_universe`, parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label={`Retransmit Duration (seconds)`} description="Maximum time the system will continue retransmitting the last DMX frame after new data stops arriving.">
        <label>
          <input 
            type="checkbox" 
            role="switch" 
            checked={endless}
            onInput={(e) => {
              updateConfig(`${modulePrefix}_repeat_time_endless`, e.target.checked);
              if (!e.target.checked) {
                updateConfig(`${modulePrefix}_repeat_time`, 60);
              }
            }}
          />
          Endlessly
        </label>
      <input
          disabled={endless}
          type={ endless ? "text" : "number" }
          min="0" max="254"
          value={configValue[`${modulePrefix}_repeat_time_endless`] ? '∞' : configValue[`${modulePrefix}_repeat_time`]}
          onInput={(e) => updateConfig(`${modulePrefix}_repeat_time`, parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label={`Retransmit Interval (ms)`} description="Time between repeated transmissions of the last DMX frame.">
        <input 
          type="number" 
          min="5" max="1275"
          value={configValue[`${modulePrefix}_repeat_interval`]}
          onInput={(e) => updateConfig(`${modulePrefix}_repeat_interval`, parseInt(e.target.value) || 0)} 
        />
      </FormField>
    </Card>
  };

  const renderArtnetConfig = () => (
    <Card title="Art-Net Configuration" notice="If enabled, data from DMX inputs will be broadcasted to Art-Net.">
      <label>
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules['artnet_out']}
          onInput={(e) => updateConfigMod('artnet_out', e.target.checked)} 
        />
        Enable Art-Net Output
      </label>
    </Card>
  );

  const renderBleConfig = () => (
    <Card
      title="Ambitful BLE Configuration"
      notice="If enabled, this will use approximately 60KB of memory and limit available scripting memory."
    >
      <label>
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules['ambitful']}
          onInput={(e) => updateConfigMod('ambitful', e.target.checked)} 
        />
        Enable Ambitful BLE
      </label>
      <FormField label="Universe" description="DMX universe to control the Ambitful lights.">
        <input 
          type="number" 
          min="0" max="32767" 
          value={configValue.ambitful_universe}
          onInput={(e) => updateConfig('ambitful_universe', parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label="DMX address" description="Start DMX address, 1-504.">
        <input 
          type="number" 
          min="1" max="504" 
          value={configValue.ambitful_addr}
          onInput={(e) => updateConfig('ambitful_addr', parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label="Ambitful channel" description="Channel number, 1-19.">
        <input 
          type="number" 
          min="1" max="19" 
          value={configValue.ambitful_channel}
          onInput={(e) => updateConfig('ambitful_channel', parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label="Ambitful groups" description="Amount of controlled groups, 1-8.">
        <input 
          type="number" 
          min="1" max="19" 
          value={configValue.ambitful_groups}
          onInput={(e) => updateConfig('ambitful_groups', parseInt(e.target.value) || 0)} 
        />
      </FormField>
    </Card>
  );

  const renderWs2812Config = () => (
    <Card title="WS2812 Addressable LED Configuration">
      <label>
        <input
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules['ws2812']}
          onInput={(e) => updateConfigMod('ws2812', e.target.checked)} 
        />
        Enable WS2812 Output
      </label>
      <FormField label="WS2812 Universe" description="DMX universe to control the LED strip.">
        <input 
          type="number" 
          min="0" max="32767" 
          value={parseInt(configValue.ws2812_universe)}
          onInput={(e) => updateConfig('ws2812_universe', parseInt(e.target.value) || 0)} 
        />
      </FormField>
    </Card>
  );

  return (
    <>
      {moduleName === "DMX" && <div class="grid">
        { renderDmxConfig('dmx', 'DMX') }
        { renderDmxConfig('dmx_2', 'Wireless DMX') }
      </div>}
      {moduleName === "Art-Net" && renderArtnetConfig()}
      {moduleName === "Ambitful BLE" && renderBleConfig()}
      {moduleName === "WS2812" && renderWs2812Config()}
    </>
  );
}
