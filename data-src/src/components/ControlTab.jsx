import { Card, FormField } from "./Common";
import { WS } from "../ws";
import { useState, useEffect } from 'preact/hooks';
import { WsConnectBtn } from "./MonitorTab";
let controlUniverse = 0;
let controlData = new Array(512).fill(0);

// Helper to generate an array of DMX channel numbers
const DMX_CHANNELS = Array.from({ length: 512 }, (_, i) => i);

export function ControlTab() {
  const [universe, setUniverse] = useState(controlUniverse);
  const [data, setData] = useState(controlData);

  // Update global signal when local universe changes
  useEffect(() => {
    controlUniverse = universe;
  }, [universe]);

  const handleSliderChange = (channelIndex, value) => {
    // Connect WS on first slider change if not connected
    if (WS.readyState() === WebSocket.CLOSED) {
        WS.connect();
    }
    
    const newData = [...controlData];
    newData[channelIndex] = parseInt(value);
    setData(newData)
    controlData = newData;
    
    WS.sendDMX(universe, newData);
  };

  return (
    <Card title="DMX Control" btn={<WsConnectBtn />}>
      <FormField label="Universe">
        <input 
          type="number"
          min="0" max="32767" 
          value={universe} 
          onInput={(e) => setUniverse(parseInt(e.target.value))} 
        />
      </FormField>
      
      <div class="dmx-controls">
        <div>
          {DMX_CHANNELS.map((channel) => (
            <div key={channel}>
              <span>
                {controlData[channel]}
              </span>
              <input 
                type="range" 
                min="0" max="255" 
                value={controlData[channel]} 
                onInput={(e) => handleSliderChange(channel, e.target.value)}
              />
              <span >
                {channel + 1}
              </span>
            </div>
          ))}
        </div>
      </div>
    </Card>
  );
}
