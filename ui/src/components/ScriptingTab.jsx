import { useState, useEffect, useRef } from "preact/hooks";
import { API } from "../api";
import { Card, Modal } from "./Common";
import { Play as PlayBtn, FileText, Trash2, StopCircle, RefreshCcw, Upload, HelpCircle, Plus, Save } from "lucide-preact";
import { ScriptEditor } from "./ScriptEditor";

const tempScriptName = '---';
let unsavedScript = "";

export function ScriptingTab() {
  const [scripts, setScripts] = useState([]);
  const [loading, setLoading] = useState(false);
  const [runningScript, setRunningScript] = useState(null);
  const [scriptState, setScriptState] = useState(null);
  const [scriptError, setScriptError] = useState(null);
  const [hideError, setHideError] = useState(null);
  const [showContentModal, setShowContentModal] = useState(false);
  const [showGuideModal, setShowGuideModal] = useState(false);
  const [currentScriptContent, setCurrentScriptContent] = useState("");
  const [currentScriptFileName, setCurrentScriptFileName] = useState("");
  const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);
  const [scriptToDelete, setScriptToDelete] = useState(null);
  const fileInputRef = useRef(null);

  const fetchScripts = async () => {
    try {
      setLoading(true);
      const data = await API.getScripts();
      if (data.running === tempScriptName) {
        data.files.unshift(tempScriptName);
      }
      setRunningScript(data.running || null);
      setScriptState(data.state || null);
      setScripts(data.files || []);
      setScriptError(data.error || null);
    } catch (e) {
      alert("Failed to fetch scripts: " + e.message);
    }
    setLoading(false);
  };

  const uploadFiles = async (files) => {
    setLoading(true);
    for (const file of Array.from(files)) {
      if (file.name.endsWith('.lua') || file.name.endsWith('.luac')) {
        try {
          const content = await file.arrayBuffer();
          await API.uploadScript(file.name, content);
        } catch (e) {
          alert(`Failed to upload ${file.name}: ${e.message}`);
        }
      }
    }
    await fetchScripts();
  };

  useEffect(() => {
    fetchScripts();

    const handleDragOver = (e) => {
      e.preventDefault();
      e.stopPropagation();
    };

    const handleDrop = (e) => {
      e.preventDefault();
      e.stopPropagation();
      if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
        uploadFiles(e.dataTransfer.files);
      }
    };

    window.addEventListener("dragover", handleDragOver);
    window.addEventListener("drop", handleDrop);

    return () => {
      window.removeEventListener("dragover", handleDragOver);
      window.removeEventListener("drop", handleDrop);
    };
  }, []);

  const handleRunScript = async (fileName) => {
    try {
      setLoading(true);
      await API.runScript(fileName);
      setTimeout(fetchScripts, 1000); // Re-fetch after 1s to get new status/error
    } catch (e) {
      alert("Failed to run script: " + e.message);
      fetchScripts();
    }
  };

  const handleStopScript = async () => {
    try {
      setLoading(true);
      await API.stopScript();
      setTimeout(fetchScripts, 100);
    } catch (e) {
      setLoading(false);
      alert("Failed to stop script: " + e.message);
    }
  };

  const handleViewScript = async (fileName) => {
    try {
      if (fileName === tempScriptName) {
        setCurrentScriptContent(unsavedScript);
        setCurrentScriptFileName("");
      } else {
        const content = await API.getScriptContent(fileName);
        setCurrentScriptContent(content);
        setCurrentScriptFileName(fileName);
      }
      setHideError(true);
      setShowContentModal(true);
    } catch (e) {
      alert("Failed to load script content: " + e.message);
    }
  };

  const handleNewScript = () => {
    setCurrentScriptContent("");
    setCurrentScriptFileName("");
    setShowContentModal(true);
  };

  const handleSaveScript = async () => {
    if (!currentScriptFileName) return;
    let name = currentScriptFileName;
    if (!name.toLowerCase().endsWith(".lua")) {
      name += ".lua";
    }
    try {
      setLoading(true);
      await API.uploadScript(name, currentScriptContent);
      setShowContentModal(false);
      await fetchScripts();
    } catch (e) {
      alert("Failed to save script: " + e.message);
    }
    setLoading(false);
  };

  const handleRunStream = async () => {
    try {
      setLoading(true);
      setScriptError(null);
      setHideError(false);
      unsavedScript = currentScriptContent;
      await API.runStream(currentScriptContent);
      setTimeout(fetchScripts, 500);
    } catch (e) {
      alert("Failed to run script: " + e.message);
    }
    setLoading(false);
  };

  const handleDeleteScript = async (fileName) => {
    setScriptToDelete(fileName);
    setShowDeleteConfirm(true);
  };

  const confirmDelete = async () => {
    try {
      setLoading(true);
      await API.deleteScript(scriptToDelete);
      await fetchScripts();
      setShowDeleteConfirm(false);
      setScriptToDelete(null);
    } catch (e) {
      alert("Failed to delete script: " + e.message);
    }
  };

  const refreshBtn = (
    <>
      <button onClick={() => setShowGuideModal(true)} title="Scripting Guide">
        <HelpCircle />
      </button>
      <button onClick={fetchScripts} title="Refresh List" aria-busy={loading}>
        { loading ? null : <RefreshCcw /> }
      </button>
    </>
  );

  return (
    <>
      <Card
        title="Lua Scripts"
        btn={refreshBtn}
        class="scripts"
      >
        <div class="description">{`Running complex scripts may impact device performance.`}</div>
        <p class="script-actions">
          <button class="outline" onClick={handleNewScript} disabled={loading}>
            <Plus size={20} style={{ marginRight: "10px" }} />
            New Script
          </button>
          <button class="outline" onClick={() => fileInputRef.current?.click()} disabled={loading}>
            <Upload size={20} style={{ marginRight: "10px" }} />
            Upload Script (.lua)
          </button>
          <input 
            type="file" 
            ref={fileInputRef} 
            style={{ display: "none" }} 
            accept=".lua,.luac"
            multiple
            onChange={(e) => uploadFiles(e.target.files)}
          />
        </p>

        {scriptError && (
          <p class="err">{scriptError}</p>
        )}
        <ul>
          {scripts.length === 0 ? (
            <li>No scripts found.</li>
          ) : (
            scripts.map((script) => {
              const running = runningScript === script;
              const temp = running && script === tempScriptName;
              const name = temp ? '📝' : script;
              const luac = name.endsWith('.luac');
              return <li key={script} class={running ? (scriptState === 2 ? "run sleep" : "run") : null}>
                <span>{name}</span>
                <div>
                  {running ? (
                    <button class="" onClick={handleStopScript} title="Stop Script">
                      <StopCircle size={20} />
                    </button>
                  ) : (
                    <button class="outline" onClick={() => handleRunScript(script)} title="Run Script">
                      <PlayBtn size={20} />
                    </button>
                  )}
                  <button class="outline" disabled={luac} onClick={() => handleViewScript(script)} title="View Script">
                    <FileText size={20} />
                  </button>
                  <button class="outline" disabled={temp}  onClick={() => handleDeleteScript(script)} title="Delete Script">
                    <Trash2 size={20} />
                  </button>
                </div>
              </li>;
            })
          )}
        </ul>
      </Card>

      <Modal
        isOpen={showContentModal}
        title={currentScriptFileName ? `Edit Script: ${currentScriptFileName}` : "New Script"}
        onClose={() => setShowContentModal(false)}
        class="script"
        footer={
          <>
            { scriptError && !hideError ? <div class="err">{scriptError}</div> : null }
            <button class={(runningScript !== tempScriptName || hideError) && "outline"} onClick={handleRunStream} title="Run without saving">
              <PlayBtn size={20} /> Run
            </button>
            <button onClick={handleSaveScript} disabled={!currentScriptFileName} title="Save to flash">
              <Save size={20} /> Save
            </button>
          </>
        }
      >
        <input 
          type="text" 
          placeholder="Filename (e.g. effect.lua)" 
          value={currentScriptFileName}
          onInput={(e) => setCurrentScriptFileName(e.target.value)}
        />
        <ScriptEditor 
          value={currentScriptContent}
          onInput={(e) => setCurrentScriptContent(e.target.value)}
          rows={15}
        />
      </Modal>

      <Modal
        isOpen={showDeleteConfirm}
        title="Confirm Delete"
        onClose={() => setShowDeleteConfirm(false)}
        onConfirm={confirmDelete}
        confirmText="Delete"
      >
        <p>Are you sure you want to delete script: <strong>{scriptToDelete}</strong>?</p>
        <p>This action cannot be undone.</p>
      </Modal>

      <Modal
        isOpen={showGuideModal}
        title="Scripting Guide"
        onClose={() => setShowGuideModal(false)}
      >
        <div class="s-guide">
          <p>The system uses Lua version 5.5.</p>
          <p>The script's main body runs once. If it registers any DMX handlers or timers, the script stays in an event loop and dispatches them as events arrive. When the last handler unregisters and the last timer fires, the script exits.</p>
          <p>A script file named <code>init.lua</code> is run on startup.</p>

          <h4>DMX I/O:</h4>
          <ul>
            <li>
              <code>esp.dmx.send(universe, data [, network])</code><br/>
              Transmit a DMX frame. <code>data</code> should be a binary string (e.g., from <code>string.char</code>). When <code>network</code> is true, the frame is also sent via Art-Net and WebSocket.
            </li>
            <li>
              <code>esp.dmx.on(universe, function(data, universe) end)</code><br/>
              Register a callback fired whenever a new frame arrives on <code>universe</code>. Pass <code>nil</code> instead of a function to unsubscribe. Calling again for the same universe replaces the handler.
            </li>
            <li>
              <code>esp.dmx.read(universe, timeout_ms)</code><br/>
              Block until a frame arrives or <code>timeout_ms</code> elapses. Returns a binary string or <code>nil</code>. Deprecated; prefer <code>esp.dmx.on</code> for new code.
            </li>
          </ul>

          <h4>Timers:</h4>
          <ul>
            <li>
              <code>esp.setTimeout(function() end, ms)</code><br/>
              Run a function once after <code>ms</code> milliseconds. Returns an integer ID.
            </li>
            <li>
              <code>esp.setInterval(function() end, ms)</code><br/>
              Run a function every <code>ms</code> milliseconds. Returns an integer ID.
            </li>
            <li>
              <code>esp.clearTimer(id)</code><br/>
              Cancel a pending timeout or stop an interval.
            </li>
          </ul>

          <h4>Other:</h4>
          <ul>
            <li>
              <code>sleep(ms)</code><br/>
              Block execution for <code>ms</code> milliseconds. Blocks all handlers and timers — prefer <code>esp.setTimeout</code> when possible.
            </li>
            <li>
              <code>random(min, max)</code><br/>
              Random integer between <code>min</code> and <code>max</code> (inclusive). With no arguments, returns a full 32-bit integer.
            </li>
            <li>
              <code>print(message)</code> or <code>warn(message)</code><br/>
              Write a line to the system log for debugging.
            </li>
          </ul>

          <h4>Example — forward a universe:</h4>
          <pre style={{ background: "var(--pico-code-background)", padding: "10px" }}>{
`esp.dmx.on(1, function(data)
  esp.dmx.send(2, data)
end)`
          }</pre>

          <h4>Example — 40 FPS rainbow:</h4>
          <pre style={{ background: "var(--pico-code-background)", padding: "10px" }}>{
`local hue = 0
esp.setInterval(function()
  local out = {}
  for i = 1, 50 do
    out[i] = string.char((hue + i) & 0xff, 255, 255)
  end
  esp.dmx.send(10, table.concat(out))
  hue = hue + 1
end, 25)`
          }</pre>

        </div>
      </Modal>
    </>
  );
}
