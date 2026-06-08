import { parseConfig, serializeConfig, parseApRecords } from './config';

const octetStream = 'application/octet-stream';

export const API = {
  async getConfig() {
    const res = await fetch('/config');
    if (!res.ok) throw new Error('Failed to load config');
    const buffer = await res.arrayBuffer();
    return parseConfig(buffer);
  },

  async saveConfig(obj) {
    const buffer = serializeConfig(obj);

    const res = await fetch('/config', {
      method: 'PUT',
      headers: { 'content-type': octetStream },
      body: buffer
    });
    if (!res.ok) throw new Error('Failed to save config');
  },

  async getScripts() {
    const res = await fetch('/lua/list');
    if (!res.ok) throw new Error('Failed to load scripts');
    return await res.json();
  },

  async runScript(name) {
    const res = await fetch(`/lua/run/${name}`, {
      method: 'POST'
    });
    if (!res.ok) throw new Error('Failed to start script');
  },

  async runStream(content) {
    const res = await fetch('/lua/run/', {
      method: 'POST',
      body: content
    });
    if (!res.ok) throw new Error('Failed to run stream');
  },

  async stopScript() {
    const res = await fetch('/lua/kill', {
      method: 'POST'
    });
    if (!res.ok) throw new Error('Failed to stop script');
  },

  async getScriptContent(name) {
    const res = await fetch(`/lua/scripts/${name}`);
    if (!res.ok) throw new Error('Failed to load script content');
    return await res.text();
  },

  async uploadScript(name, content) {
    const res = await fetch(`/lua/scripts/${name}`, {
      method: 'PUT',
      body: content
    });
    if (!res.ok) throw new Error('Failed to upload script');
  },

  async deleteScript(name) {
    const res = await fetch(`/lua/scripts/${name}`, {
      method: 'PUT',
      body: ''
    });
    if (!res.ok) throw new Error('Failed to delete script');
  },

  async scanWifi() {
    const res = await fetch('/wifi/scan', { headers: { accept: octetStream }});
    if (!res.ok) throw new Error('WiFi scan failed');
    return parseApRecords(await res.arrayBuffer());
  },

  reboot() {
    return fetch('/config', { method: 'PUT', body: new Uint8Array(0) });
  },

  async status(signal) {
    const res = await fetch('/status', { signal });
    if (!res.ok) throw new Error('Failed to get device status');
    return await res.json();
  },
};
