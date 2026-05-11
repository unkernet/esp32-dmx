import { Buffer } from 'buffer';
import { WebSocketServer } from 'ws';
import { serializeConfig } from './src/config';

const wss = new WebSocketServer({ noServer: true });
const luaScripts = new Map();
luaScripts.set('init.lua', `while true do
  local data = dmx_read(1, 100)
  if data then
    dmx_send(2, data, true)
  end
  sleep(10)
end`);
let runningScript = 'init.lua';
let lastError = null;
const start = Date.now();

let cfg = new Uint8Array(serializeConfig({
  sta_ssid: "ESP-DMX-STA",
  sta_password: "12345678",
  sta_dhcp_enabled: 1,
  sta_ip_cidr: '192.168.1.2/24',
  ap_ssid: "ESP-DMX-AP",
  ap_password: "87654321",

  enabled_modules: {
    dmx_in: true,
    dmx_out: true,
    dmx_2_in: true,
    dmx_2_out: true,
    artnet_out: true,
    ambitful: false,
    ws2812: true,
  },

  dmx_in_universe: 0,
  dmx_out_universe: 1,
  dmx_repeat_interval: 10,
  dmx_repeat_time_endless: true,

  dmx_2_in_universe: 2,
  dmx_2_out_universe: 3,
  dmx_2_repeat_interval: 10,
  dmx_2_repeat_time: 60,

  ambitful_universe: 10,
  ambitful_addr: 1,
  ambitful_channel: 2,
  ambitful_groups: 4,

  ws2812_universe: 20,
}));

export const mockApi = {
  name: 'mock-api',
  configureServer(server) {

    server.middlewares.use((req, res, next) => {

      if (req.url === '/config') {
        if (req.method === 'GET') {
          res.setHeader('Content-Type', 'application/octet-stream');
          res.end(cfg);
          return;
        } else if (req.method === 'PUT') {
          req.on('data', data => { cfg = data });
          req.on('end', () => {
            res.statusCode = 200;
            res.end('OK');
          });
          return;
        }
      }

      if (req.url === '/status') {
        let tmp;
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({
          uptime: ((Date.now() - start) / 1000) | 0,
          heap: {
            total: 1024 * 120,
            free: tmp = (1024 * 100 - (Math.random() * 1024 * 10 | 0)),
            block: tmp - 10 * 1024 - (Math.random() * 1024 * 10 | 0),
            min: 1024 * 16,
          },
          psram: {
            total: 1024 * 300,
            free: 1024 * 100,
            block: 1024 * 80,
            min: 1024 * 12,
          }
        }));
      }

      if (req.url === '/lua/list') {
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify({
          scripts: Array.from(luaScripts.keys()),
          running: runningScript,
          error: lastError
        }));
        return;
      }

      if (req.url === '/lua/run' && req.method === 'POST') {
        let body = '';
        req.on('data', d => body += d);
        req.on('end', () => {
          if (body === 'error.lua') {
            lastError = 'Error: Mock runtime error';
            runningScript = null;
          } else {
            runningScript = body;
            lastError = null;
          }
          res.end('OK');
        });
        return;
      }

      if (req.url === '/lua/kill' && req.method === 'POST') {
        runningScript = null;
        lastError = null; // Clear error on kill
        res.end('OK');
        return;
      }

      if (req.url.startsWith('/lua/scripts/')) {
        const name = req.url.split('/lua/scripts/')[1];
        if (req.method === 'GET') {
          res.end(luaScripts.get(name) || '');
          return;
        } else if (req.method === 'PUT') {
          let body = '';
          req.on('data', d => body += d);
          req.on('end', () => {
            if (body === '') { // Empty body means delete
              luaScripts.delete(name);
            } else {
              luaScripts.set(name, body);
            }
            res.end('OK');
          });
          return;
        }
      }

      if (req.url === '/wifi/scan') {
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify([
          { ssid: 'Mock-WiFi-1', rssi: -50, bssid: '00:11:22:33:44:55', channel: 1, secure: 3 },
          { ssid: 'Mock-WiFi-3', rssi: -90, bssid: '66:77:88:99:AA:BB', channel: 2, secure: 3 },
          { ssid: 'Mock-WiFi-2', rssi: -70, bssid: '66:77:88:99:AA:BB', channel: 6, secure: 0 },
        ]));
        return;
      }
      next();
    });

    server.httpServer.on('upgrade', (request, socket, head) => {
      if (request.url === '/ws') {
        wss.handleUpgrade(request, socket, head, (ws) => {
          wss.emit('connection', ws, request);
        });
      }
    });
    let wsCon = new Set();
    wss.on('connection', (ws) => {
      wsCon.add(ws);
      const universe = 1;
      const len = 120;
      const data = Buffer.alloc(len + 2);
      data.writeUInt16LE(universe, 0);
      let frame = 0;
      const tmr = setInterval(() => {
        for (let i = 0; i < len; i++) {
          const d = Math.abs((frame % len) - i);
          if (d > 10) {
            data[i + 2] = 0;
          } else {
            data[i + 2] = (10 - d) * 25;
          }
        }
        frame++;
        ws.send(data);
      }, 250);
      ws.on('message', (data) => {
        wsCon.forEach(ws => { ws.send(data); });
      });
      ws.on('close', () => {
        wsCon.delete(ws);
        clearInterval(tmr);
      });
    });

    server.ws.on('connection', (socket, req, ...args) => {
      console.log(req.url);
      if (req.url !== '/ws') {
        return;
      }
      console.log('WS Client connected', socket, args);
      let frame = 0;
      const interval = setInterval(() => {
        const universe = 1;
        const data = Buffer.alloc(514);
        data.writeUInt16LE(universe, 0);
        
        // Generate slowly changing DMX data
        const baseValue = Math.floor(127 + 127 * Math.sin(frame / 20));
        for (let i = 0; i < 512; i++) {
          // Add some variation per channel
          const channelValue = (baseValue + i) % 256;
          data[i + 2] = channelValue;
        }
        
        // In Vite, we can send a custom event with a base64 encoded payload
        server.ws.send('dmx-data', {
            universe: universe,
            data: data.slice(2).toString('base64')
        });
        
        frame++;
      }, 100); // 10fps

      socket.on('close', () => {
        clearInterval(interval);
      });
    });

  }
};
