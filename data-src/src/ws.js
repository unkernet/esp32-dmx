import { wsStatus, monitorData, monitorHistory } from "./signals";

let ws = null;
let lastSendTmr = null;
let delayedData;
const SEND_INTERVAL = 100; // 10 packets per second

export const WS = {
  connect() {
    if (ws) return;
    
    wsStatus.value = "connecting";
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const host = window.location.host;
    const wsUrl = `${protocol}//${host}/ws`;
    // const wsUrl = 'ws://esp-dmx.local/ws';

    ws = new WebSocket(wsUrl);
    ws.binaryType = "arraybuffer";

    ws.onopen = () => {
      wsStatus.value = "connected";
    };

    ws.onclose = () => {
      wsStatus.value = "disconnected";
      ws = null;
    };

    ws.onerror = (err) => {
      console.error("WS Error", err);
      wsStatus.value = "error";
    };

    ws.onmessage = (event) => {
      if (event.data instanceof ArrayBuffer) {
        const view = new Uint8Array(event.data);
        if (view.length > 2) {
          const universe = view[0] | (view[1] << 8);
          const data = view.slice(2);
          
          // Update monitor data signal
          const current = { ...monitorData.value };
          if (current[universe] && current[universe].length > data.length) {
            current[universe].set(data, 0);
          } else {
            current[universe] = data;
          }
          monitorData.value = current;

          // Update history
          // Sending only one signal, monitorData, is enought
          // const h = { ...monitorHistory.value };
          const h = monitorHistory.value;
          if (!h[universe]) h[universe] = [];
          h[universe].push(data);
          if (h[universe].length > 100) h[universe].shift();
          // monitorHistory.value = h;
        }
      }
    };
  },

  readyState() {
    return ws && ws.readyState || WebSocket.CLOSED;
  },

  disconnect() {
    if (ws) {
      ws.close();
      ws = null;
    }
  },

  sendDMX(universe, data) {
    if (this.readyState() !== WebSocket.OPEN) {
      return;
    }

    if (lastSendTmr) {
      delayedData = {universe, data};
      return;
    }

    delayedData = null;

    lastSendTmr = setTimeout(() => {
      lastSendTmr = null;
      if (delayedData) {
        WS.sendDMX(delayedData.universe, delayedData.data);
      }
    }, SEND_INTERVAL);

    // Find the last non-zero index, but at least 16
    let lastNonZero = 15;
    for (let i = 511; i >= 16; i--) {
      if (data[i] > 0) {
        lastNonZero = i;
        break;
      }
    }

    const len = lastNonZero + 1;
    const packet = new Uint8Array(2 + len);
    packet[0] = universe & 0xff;
    packet[1] = (universe >> 8) & 0xff;
    for (let i = 0; i < len; i++) {
      packet[2 + i] = data[i];
    }

    ws.send(packet);
  }
};
