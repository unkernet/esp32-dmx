import { Card, FormField, Modal } from "./Common";
import { API } from "../api";
import { Clock, HardDrive, RefreshCcw } from "lucide-preact";
import { useEffect, useState } from 'preact/hooks';
import { config } from "../signals";

function MemUsage({ mem, label }) {
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
  const [waitReboot, setWaitReboot] = useState(false);
  const { value: configValue } = config;
  const dev_name = configValue?.meta?.dev_name;

  const handleReboot = async () => {
    try {
      setWaitReboot(true);
      await API.reboot();
      setInterval(() => { location.reload(); }, 1000);
    } catch (e) {
      setWaitReboot(false);
      alert("Reboot failed: " + e.message);
    }
  };

  useEffect(() => {
    let timeoutId;
    const controller = new AbortController();

    const updateStatus = async () => {
      try {
        const status = await API.status(controller.signal);
        setSystemStatus(status);
        timeoutId = setTimeout(updateStatus, 3000);
      } catch (e) {
        if (e.name !== 'AbortError') {
          console.error("Failed to update status:", e);
          timeoutId = setTimeout(updateStatus, 5000);
        }
      }
    };

    updateStatus();

    return () => {
      clearTimeout(timeoutId);
      controller.abort();
    };
  }, []);

  const formatUptime = (seconds) => {
    const d = Math.floor(seconds / (3600 * 24));
    const h = Math.floor((seconds % (3600 * 24)) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    return `${d}d ${h}h ${m}m ${s}s`;
  };

  // if (!systemStatus) {
  //   return <Card title="System Information"/>;
  // }

  return (
    <Card title="System Information" class="sysinfo">
      { dev_name ? 
        <FormField>
          <p>
            <strong>ESP-DMX-{dev_name}</strong><br/>
            <a href={`http://esp-dmx.local/`}>{`http://esp-dmx.local/`}</a><br/>
            <a href={`http://esp-dmx-${dev_name.toLocaleLowerCase()}.local/`}>{`http://esp-dmx-${dev_name.toLocaleLowerCase()}.local/`}</a><br/>
          </p>
        </FormField>
        : null }

      { systemStatus ? <>
        <FormField label="Uptime">
          <p style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
            <Clock size={20} />
            <span>{formatUptime(systemStatus.uptime)}</span>
          </p>
        </FormField>

        <MemUsage label="Memory usage" mem={systemStatus.heap} />
      </> : <p aria-busy={true} /> }


      <button onClick={() => { setShowRebootConfirm(true) }} class="secondary">
        <RefreshCcw size={20} /> Reboot Device
      </button>

      <Modal
        isOpen={showRebootConfirm}
        title="Reboot"
        onClose={() => setShowRebootConfirm(false)}
        onConfirm={handleReboot}
        confirmText="Reboot"
        busy={waitReboot}
      >
        <p>Are you sure you want to reboot the device?</p>
      </Modal>
    </Card>
  );
}
