import { signal } from "@preact/signals";

export const activeTab = signal("wifi");
export const config = signal(null);
export const isDirty = signal(false);

export const wsStatus = signal("disconnected");
export const monitorData = signal({}); // universe -> data

export const scripts = signal([]);
export const runningScript = signal(null);
export const scriptError = signal(null);
