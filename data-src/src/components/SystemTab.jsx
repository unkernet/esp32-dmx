import { Card, FormField, Modal } from "./Common";
import { API } from "../api";
import { Clock, HardDrive, RefreshCcw } from "lucide-preact";
import { useEffect, useState } from 'preact/hooks';

let updateTmr;

function MemUsage({ mem, label }) {
  const memUsagePercent = mem.total > 0 
    ? ((mem.total - mem.free) / mem.total) * 100
    : 0;

  return <FormField label={label}>
    <p style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', margin: 0 }}>
      <HardDrive size={20} />
      <span>{Math.floor(mem.free / 1024)} KB free / {Math.floor(mem.total / 1024)} KB total</span>
    </p>
    <div class="mem" style={{
      '--free': mem.free,
      '--total': mem.total,
      '--block': mem.block,
      '--min': mem.min,
    }} />
  </FormField>
}

export function SystemTab() {
  const [systemStatus, setSystemStatus] = useState(null);
  const [showRebootConfirm, setShowRebootConfirm] = useState(false);

  const handleReboot = async () => {
    try {
      await API.reboot();
      setInterval(() => { location.reload(); }, 1000);
    } catch (e) {
      alert("Reboot failed: " + e.message);
    }
  };

  const updateStatus = async () => {
      const status = await API.status();
      setSystemStatus(status);
      updateTmr = setTimeout(updateStatus, 3000);
  };

  useEffect(() => {
    updateStatus();
    return () => {
      clearTimeout(updateTmr);
    }
  }, []);

  const formatUptime = (seconds) => {
    const d = Math.floor(seconds / (3600 * 24));
    const h = Math.floor((seconds % (3600 * 24)) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    return `${d}d ${h}h ${m}m ${s}s`;
  };

  if (!systemStatus) {
    return <Card title="System Information"/>;
  }

  return (
    <Card title="System Information" class="sysinfo">
      <FormField label="Uptime">
        <p style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <Clock size={20} />
          <span>{formatUptime(systemStatus.uptime)}</span>
        </p>
      </FormField>

      <MemUsage label="Memory usage" mem={systemStatus.heap} />

      <button onClick={() => { setShowRebootConfirm(true) }} class="secondary">
        <RefreshCcw size={20} /> Reboot Device
      </button>

      <Modal
        isOpen={showRebootConfirm}
        title="Reboot"
        onClose={() => setShowRebootConfirm(false)}
        onConfirm={handleReboot}
        confirmText="Reboot"
      >
        <p>Are you sure you want to reboot the device?</p>
      </Modal>
    </Card>
  );
}
