import { Buffer } from 'buffer';
import { WebSocketServer } from 'ws';
import { serializeConfig, serializeMeta, serializeAp } from './src/config.js';

const wss = new WebSocketServer({ noServer: true });
const luaScripts = new Map();
luaScripts.set('init.lua', `while true do
  local data = dmx_read(1, 100)
  if data then
    dmx_send(2, data, true)
  end
  sleep(10)
end`);
luaScripts.set('bin.luac', ``);
let runningScript = 'init.lua';
let lastError = null;
const start = Date.now();

let cfg = new Uint8Array(serializeConfig({
  magic: 0x5844,
  version: 1,
  wifi: {
    sta: {
        ssid: "ESP-DMX-STA",
        password: "12345678",
        dhcp_enabled: 1,
        ip_cidr: '192.168.1.2/24',
    },
    ap: {
        ssid: "ESP-DMX-AP",
        password: "87654321",
    }
  },

  enabled_modules: {
    dmx_0_in: true,
    dmx_0_out: true,
    dmx_1_in: true,
    dmx_1_out: true,
    artnet_out: true,
    artnet_in: true,
    artnet_ws: false,
    ambitful: false,
    ws2812_0: true,
    lua: true,
  },

  dmx_ports: [
    { in_universe: 0, out_universe: 1, repeat_interval: 10, repeat_time: 255 },
    { in_universe: 2, out_universe: 3, repeat_interval: 10, repeat_time: 254 },
    { in_universe: 0, out_universe: 0, repeat_interval: 0, repeat_time: 0 },
    { in_universe: 0, out_universe: 0, repeat_interval: 0, repeat_time: 0 },
  ],

  ws2812_ports: [
    { universe: 20 },
    { universe: 0 },
    { universe: 0 },
    { universe: 0 },
  ],

  ambitful: {
    universe: 10,
    addr: 1,
    channel: 2,
    groups: 4,
  },

  reserved: "",
}));

const metaCfg = new Uint8Array(serializeMeta({
  supported: {
    dmx_0_in: true,
    dmx_0_out: true,
    dmx_1_in: true,
    dmx_1_out: true,
    artnet_out: true,
    artnet_ws: true,
    ambitful: true,
    ws2812_0: true,
    ws2812_1: true,
    lua: true,
  },
  dev_name: 'TEST',
  dmx_name: ['DMX', 'Wirecless DMX', '', ''],
}));

export const mockApi = {
  name: 'mock-api',
  configureServer(server) {

    const tempScriptName = '---';

    server.middlewares.use((req, res, next) => {

      if (req.url === '/config') {
        if (req.method === 'GET') {
          res.setHeader('Content-Type', 'application/octet-stream');
          const data = new Uint8Array(cfg.byteLength + metaCfg.byteLength);
          data.set(cfg, 0);
          data.set(metaCfg, cfg.byteLength);
          res.end(data);
          return;
        } else if (req.method === 'PUT') {
          res.statusCode = 200;
          req.on('data', data => {
            data = new Uint8Array(data);
            let match = true;
            // Compare header
            for (let i = 0; i < 4; i++) {
              if (data[i] !== cfg[i]) {
                match = false;
                break;
              }
            }
            if (match) {
              cfg = data;
            } else {
              res.statusCode = 500;
            }
          });
          req.on('end', () => {
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

      if (req.url.startsWith('/lua/run/') && req.method === 'POST') {
        runningScript = req.url.substr('/lua/run/'.length) || tempScriptName;
        lastError = null;
        if (runningScript === 'error.lua') {
            lastError = 'Error: Mock runtime error';
            runningScript = null;
        }
        if (runningScript === tempScriptName) {
          let data = '';
          req.on('data', chunk => { data += chunk; });
          req.on('end', () => {
            if (data.includes('error')) {
              lastError = 'Error: Mock runtime error';
              runningScript = null;
            }
          });
        }
        res.end('OK');
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
        res.setHeader('Content-Type', 'application/octet-stream');
        res.end(new Uint8Array(serializeAp([
          { ssid: 'Mock-WiFi-1', rssi: -50, channel: 1, authmode: 3 },
          { ssid: 'Mock-WiFi-3', rssi: -90, channel: 2, authmode: 3 },
          { ssid: 'Mock-WiFi-2', rssi: -70, channel: 6, authmode: 0 },
        ])));
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
            data[i + 2] = (Math.cos(d / 10 * Math.PI) + 1) / 2 * 255;
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
