import { Card, FormField } from "./Common";
import { monitorData, wsStatus } from "../signals";
import { WS } from "../ws";
import { Signal, WifiOff, Cable } from "lucide-preact";
import { useState, useEffect } from 'preact/hooks';

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
  const { value: data } = monitorData;
  // const data = monitorData.value[selectedUniverse];

  // useEffect(() => {
  //   WS.connect(); // Connect when monitor tab is active
  //   return () => WS.disconnect();
  // }, []);

  const universes = Object.keys(data);

  useEffect(() => {
    if (!selectedUniverse && universes.length) {
      setSelectedUniverse(universes[0]);
    }
  }, [data]);

  // Placeholder for rendering squares for 512 channels
  const renderDmxSquares = () => {
    const selectedData = selectedUniverse && data[selectedUniverse];
    if (!selectedData) return null;

    return <div class="dmx-mon">
      {Array.from({ length: selectedData.length }).map((_, i) => {
        const value = selectedData[i] || 0;
        return <div key={i} data-val={value} title={`CH ${i+1}`}/>;
      })}
    </div>;
  };

  if (universes.length === 0) {
    return <Card title="DMX Monitor" btn={<WsConnectBtn />}>
      { wsStatus.value === 'connected' ? 'No data' : 'Not connected' }
    </Card>
  }

  return (
    <Card title="DMX Monitor" btn={<WsConnectBtn />}>
      <FormField label="Universe">
        <select
          onInput={(e) => setSelectedUniverse(parseInt(e.target.value))}
        >
          {universes.map((u) => <option value={u}>{u}</option>)}
        </select>
      </FormField>
      {renderDmxSquares()}
    </Card>
  );
}
