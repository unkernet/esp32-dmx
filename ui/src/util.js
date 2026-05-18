import { config } from "./signals";

export function updateConfig(path, val) {
    const keys = path.split('.');
    const newConfig = JSON.parse(JSON.stringify(config.value));
    let target = newConfig;
    for (let i = 0; i < keys.length - 1; i++) {
        let key = keys[i];
        if (key.match(/^\d+$/)) {
          key = +key;
        }
        target = target[key];
    }
    let lastKey = keys[keys.length - 1];
    if (lastKey.match(/^\d+$/)) {
      lastKey = +lastKey;
    }
    target[lastKey] = val;
    config.value = newConfig;
};

export function ccs(...list) {
    return list.filter(Boolean).join(' ');
}
