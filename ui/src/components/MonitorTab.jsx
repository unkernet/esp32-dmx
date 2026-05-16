import { Card, FormField } from "./Common";
import { monitorData, wsStatus, monitorHistory } from "../signals";
import { WS } from "../ws";
import { Cable } from "lucide-preact";
import { useState, useEffect, useRef } from 'preact/hooks';

const { value: history } = monitorHistory;

export function WsConnectBtn() {
  return <button
    class={wsStatus.value === 'connected' ? '' : 'secondary'}
    aria-busy={wsStatus.value === 'connecting'}
    onClick={() => {
      if (wsStatus.value === 'connected') {
        WS.disconnect();
      } else {
        WS.connect();
      }
    }}
  >
    { wsStatus.value === 'connecting' ? null : <Cable/> }
  </button>;
}

export function MonitorTab() {
  const [selectedUniverse, setSelectedUniverse] = useState(null);
  const [selectedChannel, setSelectedChannel] = useState(null);
  const { value: data } = monitorData;
  const canvasRef = useRef(null);

  const universes = Object.keys(data);

  useEffect(() => {
    if (selectedUniverse === null && universes.length) {
      setSelectedUniverse(parseInt(universes[0]));
    }
  }, [data, selectedUniverse]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || selectedChannel === null || !history[selectedUniverse]) return;

    const ctx = canvas.getContext('2d');
    const frames = history[selectedUniverse];
    const width = canvas.width;
    const height = canvas.height;
    
    ctx.clearRect(0, 0, width, height);
    ctx.beginPath();
    ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--primary').trim();
    ctx.lineWidth = 3;

    const len = 100;

    const step = width / (len - 1);
    for (let i = frames.length - 1; i >= 0; i--) {
        const frame = frames[i];
        const x = (i + len - frames.length) * step;
        const y = (1 -(frame[selectedChannel] ?? 0) / 255) * height;
        
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }, [data, selectedUniverse, selectedChannel]);

  const selectedData = selectedUniverse !== null && data[selectedUniverse];

  return (
    <Card title="DMX Monitor" btn={<WsConnectBtn />} class="monitor">
      <FormField label="Universe">
        <select
          value={selectedUniverse}
          onInput={(e) => {
            setSelectedUniverse(parseInt(e.target.value));
            setSelectedChannel(null); // Reset channel on universe change
          }}
        >
          {universes.map((u) => <option value={u}>{u}</option>)}
        </select>
      </FormField>
      <div class="dmx-data">
        { selectedData !== null && (
          <div class="dmx-mon">
            {Array.from({ length: selectedData.length }).map((_, i) => {
              const value = selectedData[i] || 0;
              return <div key={i} data-val={value} title={`CH ${i+1}`} class={selectedChannel === i ? 'selected' : ''} onClick={() => setSelectedChannel(i)}/>;
            })}
          </div>
        ) }
        {selectedChannel !== null && (
          <div class="dmx-history">
            <strong>CH {selectedChannel + 1}</strong>
            <canvas ref={canvasRef} width="400" height="200" />
          </div>
        )}
      </div>
    </Card>
  );
}
