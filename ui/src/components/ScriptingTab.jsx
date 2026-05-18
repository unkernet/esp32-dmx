import { useState, useEffect, useRef } from "preact/hooks";
import { API } from "../api";
import { Card, Modal } from "./Common";
import { Play as PlayBtn, FileText, Trash2, StopCircle, RefreshCcw, Upload, HelpCircle, Plus, Save } from "lucide-preact";
// import { ccs } from '../util';

let unsavedScript = "";

export function ScriptingTab() {
  const [scripts, setScripts] = useState([]);
  const [loading, setLoading] = useState(false);
  const [runningScript, setRunningScript] = useState(null);
  const [scriptError, setScriptError] = useState(null);
  const [hideError, setHideError] = useState(null);
  const [showContentModal, setShowContentModal] = useState(false);
  const [showGuideModal, setShowGuideModal] = useState(false);
  const [currentScriptContent, setCurrentScriptContent] = useState("");
  const [currentScriptFileName, setCurrentScriptFileName] = useState("");
  const [showDeleteConfirm, setShowDeleteConfirm] = useState(false);
  const [scriptToDelete, setScriptToDelete] = useState(null);
  const fileInputRef = useRef(null);
  const editorRef = useRef(null);

  const fetchScripts = async () => {
    try {
      setLoading(true);
      const data = await API.getScripts();
      if (data.running === '|') {
        data.scripts.unshift('|');
      }
      setRunningScript(data.running || null);
      setScripts(data.scripts || []);
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
      const content = await API.getScriptContent(fileName);
      setCurrentScriptContent(content);
      setCurrentScriptFileName(fileName);
      setHideError(true);
      setShowContentModal(true);
    } catch (e) {
      alert("Failed to load script content: " + e.message);
    }
  };

  const handleNewScript = () => {
    setCurrentScriptContent(unsavedScript);
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
      if (!currentScriptFileName) {
        unsavedScript = currentScriptContent;
      }
      await API.runStream(currentScriptContent);
      setTimeout(fetchScripts, 500);
    } catch (e) {
      alert("Failed to run script: " + e.message);
    }
    setLoading(false);
  };

  const handleEditorKeyDown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      const { selectionStart, selectionEnd, value } = e.target;
      const before = value.substring(0, selectionStart);
      const after = value.substring(selectionEnd);
      const line = before.split('\n').pop();
      const spaces = line.match(/^\s*/)[0];
      
      const newValue = before + '\n' + spaces + after;
      setCurrentScriptContent(newValue);
      
      const newPos = selectionStart + 1 + spaces.length;
      setTimeout(() => {
        if (editorRef.current) {
          editorRef.current.selectionStart = editorRef.current.selectionEnd = newPos;
        }
      }, 0);
    }
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
              const temp = running && script === '|';
              const name = temp ? '📝' : script;
              const luac = name.endsWith('.luac');
              return <li key={script} class={running ? "run" : null}>
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
                  <button class="outline" disabled={temp || luac} onClick={() => handleViewScript(script)} title="View Script">
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
            <button class={(runningScript !== '|' || hideError) && "outline"} onClick={handleRunStream} title="Run without saving">
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
        <textarea 
          ref={editorRef}
          class="script-src"
          value={currentScriptContent}
          onInput={(e) => setCurrentScriptContent(e.target.value)}
          onKeyDown={handleEditorKeyDown}
          spellcheck={false}
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
          <p>Scripts usually include an <strong>endless loop</strong> to process or generate DMX data in real-time.</p>
          <p>Script file named <code>init.lua</code> will be run on startup.</p>

          <h4>Functions:</h4>
          <ul>
            <li>
              <code>dmx.send(universe, data, network)</code><br/>
              Transmit data. <code>data</code> should be a binary string (e.g., from <code>string.char</code>). If <code>network</code> is true, data is also sent via Art-Net and WebSocket.
            </li>
            <li>
              <code>dmx.read(universe, timeout)</code><br/>
              Wait up to <code>timeout</code> ms for data. Returns binary string or <code>nil</code>.
            </li>
            <li>
              <code>sleep(ms)</code><br/>
              Pause execution for the specified milliseconds.
            </li>
            <li>
              <code>random(min, max)</code><br/>
              Generate a true random integer between <code>min</code> and <code>max</code> (inclusive). If no arguments, returns a full 32-bit integer.
            </li>
            <li>
              <code>print(string)</code><br/>
              Print a message to the system log for debugging.
            </li>
          </ul>

          <h4>Example:</h4>
          <pre style={{ background: "var(--pico-code-background)", padding: "10px" }}>{
`while true do
  local data = dmx.read(1, 100) -- read data from universe 1
  if data then
    dmx.send(2, data) -- output it to universe 2
  end
  sleep(100)
end`
          }</pre>
        </div>
      </Modal>
    </>
  );
}
