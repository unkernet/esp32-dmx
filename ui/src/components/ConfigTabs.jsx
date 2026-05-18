import { Card, FormField } from "./Common";
import { config } from "../signals";
import { updateConfig } from '../util';

export function ConfigTabs({ moduleName }) {
  const { value: configValue } = config;
  const { supported, dmx_name } = configValue?.meta || { supported: 0 };
  if (!configValue) return <p>Loading configuration...</p>;

  const renderDmxConfig = (portIndex) => {
    const port = configValue.dmx_ports[portIndex];
    const endless = port.repeat_time_endless;
    const modPrefix = `dmx_${portIndex}`;
    const dmxIn = supported[`${modPrefix}_in`];
    const dmxOut = supported[`${modPrefix}_out`];

    if (!dmxIn && !dmxOut) {
      return null;
    }

    return <Card title={`${dmx_name[portIndex]} Configuration`}>
      { dmxIn ? <>
        <FormField label="Enable Input">
          <input 
            type="checkbox" 
            role="switch" 
            checked={configValue.enabled_modules[`${modPrefix}_in`]}
            onInput={(e) => updateConfig(`enabled_modules.${modPrefix}_in`, e.target.checked)} 
          />
        </FormField>
        <FormField label={`Input Universe`} description="Assign incoming DMX data to this universe.">
          <input 
            type="number" 
            min="0" max="32767" 
            value={port.in_universe}
            onInput={(e) => updateConfig(`dmx_ports.${portIndex}.in_universe`, parseInt(e.target.value) || 0)} 
          />
        </FormField>
        </> : null }
      { (dmxIn && dmxOut) ? <hr/> : null }
      { dmxOut ? <>
        <FormField label="Enable Output">
          <input 
            type="checkbox" 
            role="switch" 
            checked={configValue.enabled_modules[`${modPrefix}_out`]}
            onInput={(e) => updateConfig(`enabled_modules.${modPrefix}_out`, e.target.checked)} 
          />
        </FormField>
        <FormField label={`Output Universe`} description="Output data from this universe to the DMX port.">
          <input 
            type="number" 
            min="0" max="32767" 
            value={port.out_universe}
            onInput={(e) => updateConfig(`dmx_ports.${portIndex}.out_universe`, parseInt(e.target.value) || 0)} 
          />
        </FormField>
        <FormField label={`Retransmit Duration (seconds)`} description="Maximum time the system will continue retransmitting the last DMX frame after new data stops arriving.">
          <FormField label="Endlessly">
            <input 
              type="checkbox" 
              role="switch" 
              checked={endless}
              onInput={(e) => {
                updateConfig(`dmx_ports.${portIndex}.repeat_time_endless`, e.target.checked);
                if (!e.target.checked) {
                  updateConfig(`dmx_ports.${portIndex}.repeat_time`, 60);
                }
              }}
            />
          </FormField>
          <input
              disabled={endless}
              type={ endless ? "text" : "number" }
              min="0" max="254"
              value={endless ? '∞' : port.repeat_time}
              onInput={(e) => updateConfig(`dmx_ports.${portIndex}.repeat_time`, parseInt(e.target.value) || 0)} 
            />
        </FormField>
        <FormField label={`Retransmit Interval (ms)`} description="Time between repeated transmissions of the last DMX frame.">
          <input 
            type="number"
            min="1" max="60000"
            value={port.repeat_interval}
            onInput={(e) => updateConfig(`dmx_ports.${portIndex}.repeat_interval`, parseInt(e.target.value) || 0)} 
          />
        </FormField>
      </> : null }
    </Card>
  };

  const renderArtnetConfig = () => (
    <Card title="Art-Net Configuration">
      <FormField label="Enable Art-Net Output" description="If enabled, data from DMX inputs will be broadcasted to Art-Net.">
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules.artnet_out}
          onInput={(e) => updateConfig('enabled_modules.artnet_out', e.target.checked)} 
        />
      </FormField>
      <FormField label="Enable Art-Net Input" description="If enabled, data from Art-Net will be send to the corresponding to DMX output.">
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules.artnet_in}
          onInput={(e) => updateConfig('enabled_modules.artnet_in', e.target.checked)} 
        />
      </FormField>
      <FormField label="Art-Net ⇄ Websocket Forwarding" description={"Enables data forwarding between Art-Net and Websocket.\nThis allows you to monitor Art-Net traffic in the Monitor tab and control Art-Net devices from the Control tab, but may impact device performance."}>
        <input 
          type="checkbox" 
          role="switch" 
          disabled={!configValue.enabled_modules.artnet_out && !configValue.enabled_modules.artnet_in}
          checked={configValue.enabled_modules.artnet_ws}
          onInput={(e) => updateConfig('enabled_modules.artnet_ws', e.target.checked)} 
        />
      </FormField>
    </Card>
  );

  const renderBleConfig = () => (
    <Card
      title="Ambitful BLE Configuration"
      notice={config.value.meta.supported.lua ? "If enabled, this will use approximately 60KB of memory and limit available scripting memory." : null}
    >
      <FormField label="Enable Ambitful BLE">
        <input 
          type="checkbox" 
          role="switch" 
          checked={configValue.enabled_modules.ambitful}
          onInput={(e) => updateConfig('enabled_modules.ambitful', e.target.checked)} 
        />
      </FormField>
      <FormField label="Universe" description="DMX universe to control the Ambitful lights.">
        <input 
          type="number" 
          min="0" max="32767" 
          value={configValue.ambitful.universe}
          onInput={(e) => updateConfig('ambitful.universe', parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label="DMX address" description="Start DMX address, 1-504.">
        <input 
          type="number" 
          min="1" max="504" 
          value={configValue.ambitful.addr}
          onInput={(e) => updateConfig('ambitful.addr', parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label="Ambitful channel" description="Channel number, 1-19.">
        <input 
          type="number" 
          min="1" max="19" 
          value={configValue.ambitful.channel}
          onInput={(e) => updateConfig('ambitful.channel', parseInt(e.target.value) || 0)} 
        />
      </FormField>
      <FormField label="Ambitful groups" description="Amount of controlled groups, 1-8.">
        <input 
          type="number" 
          min="1" max="19" 
          value={configValue.ambitful.groups}
          onInput={(e) => updateConfig('ambitful.groups', parseInt(e.target.value) || 0)} 
        />
      </FormField>
    </Card>
  );

  const renderWs2812Config = () => {
    const ws2812Supported = Object.keys(supported).filter(k => k.startsWith('ws2812_') && supported[k]);

    return <Card title="WS2812 Addressable LED Configuration">
      {ws2812Supported.map((mod, i) => {
        const port = configValue.ws2812_ports[i];
        return (
          <div key={mod}>
            {ws2812Supported.length > 1 && <h6>WS2812 Channel {i}</h6>}
            <FormField label="Enable Output">
              <input
                type="checkbox" 
                role="switch" 
                checked={configValue.enabled_modules[mod]}
                onInput={(e) => updateConfig(`enabled_modules.${mod}`, e.target.checked)} 
              />
            </FormField>
            <FormField label="WS2812 Universe" description="DMX universe to control the LED strip.">
              <input 
                type="number" 
                min="0" max="32767" 
                value={parseInt(port.universe)}
                onInput={(e) => updateConfig(`ws2812_ports.${i}.universe`, parseInt(e.target.value) || 0)} 
              />
            </FormField>
          </div>
        );
      })}
    </Card>
  };


  return (
    <>
      {moduleName === "DMX" && <div class="grid">
        { renderDmxConfig(0) }
        { renderDmxConfig(1) }
        { renderDmxConfig(2) }
        { renderDmxConfig(3) }
      </div>}
      {moduleName === "Art-Net" && renderArtnetConfig()}
      {moduleName === "Ambitful BLE" && renderBleConfig()}
      {moduleName === "WS2812" && renderWs2812Config()}
    </>
  );
}
