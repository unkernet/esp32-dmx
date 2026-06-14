import { useState, useEffect } from 'preact/hooks';
import { config } from "../signals";
import { API } from "../api";
import { Card, FormField } from "./Common";
import { updateConfig } from '../util';
import { Activity } from "lucide-preact";
import { mqttStatus } from '../config';

export function MqttTab() {
  const [status, setMqttStatus] = useState("loading...");

  useEffect(() => {
    let timeoutId;
    const controller = new AbortController();

    const updateStatus = async () => {
      const deviceStatus = await API.status(controller.signal);
      setMqttStatus(mqttStatus[deviceStatus.mqtt] || "unknown");
    };

    updateStatus();

    return () => {
      controller.abort();
    };
  }, []);

  const { value: configValue } = config;
  if (!configValue) return <p>Loading configuration...</p>;

  return (
    <Card title="MQTT Settings" notice="Configure MQTT broker connection for built-in DMX routing and Lua scripting.">
      <FormField label="Enable MQTT">
        <input 
          type="checkbox" 
          role="switch"
          checked={configValue.enabled_modules.mqtt}
          onInput={(e) => updateConfig('enabled_modules.mqtt', e.target.checked)}
        />
      </FormField>

      <FormField 
        label="Broker URI" 
        description="Standard MQTT URI: mqtt://user:pass@host:1883"
      >
        <input 
          type="text" 
          placeholder="mqtt://host:1883" 
          value={configValue.mqtt_broker_uri}
          onInput={(e) => updateConfig('mqtt_broker_uri', e.target.value)}
        />
      </FormField>

      <p>
        Status: {status}
      </p>

      <p>
        The device automatically subscribes to <code>dmx/universe/+/+</code>. 
        This pattern allows you to update specific DMX channels by publishing to a topic that specifies the universe and starting channel.
      </p>
      <p>
        <strong>Topic:</strong> <code>dmx/universe/&lt;universe&gt;/&lt;start_channel&gt;</code><br/>
        <strong>Payload:</strong> Comma-separated decimal values (0-255).
      </p>
      <p>
        <em>Example:</em> Publishing <code>255,128,0</code> to <code>dmx/universe/1/10</code> will set DMX channels 10, 11, and 12 of universe 1.
      </p>
      <p>
        If the <strong>Broker URI</strong> is left empty, the device will attempt to find an MQTT broker on your network using mDNS (<code>_mqtt._tcp</code>).
      </p>
      <p>
        If Home Assistant is detected, the device will automatically register as a light entity using its mDNS ID.
      </p>
    </Card>
  );
}
