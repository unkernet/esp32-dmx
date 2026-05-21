import { useState, useRef } from "preact/hooks";

const getLineInfo = (text, start, end) => {
  const lineStart = text.lastIndexOf("\n", start - 1) + 1;
  let lineEnd = text.indexOf("\n", end);
  if (lineEnd === -1) lineEnd = text.length;
  
  return {
    start: lineStart,
    end: lineEnd,
    line: text.substring(lineStart, lineEnd),
    before: text.substring(0, lineStart),
    after: text.substring(lineEnd)
  };
};

const mtKey = "application/vnd.code.copymetadata";

function getCopyMetadata(clipboardData) {
  const copymetadata = clipboardData.getData(mtKey);
  try {
    return copymetadata && JSON.parse(copymetadata);
  } catch (e) {
    return null;
  }
}

export function ScriptEditor({ value, onInput, class: className, rows = 15, placeholder = "" }) {
  const [wordWrap, setWordWrap] = useState(false);
  const textareaRef = useRef(null);

  // Helper to update text and selection
  const update = (rangeStart, rangeEnd, replacement, selectStart, selectEnd) => {
    const textarea = textareaRef.current;
    if (!textarea) return;

    textarea.focus();
    // Select only the range to be replaced
    textarea.setSelectionRange(rangeStart, rangeEnd);
    
    // execCommand('insertText') preserves the browser's native undo/redo history.
    if (!document.execCommand("insertText", false, replacement)) {
      // Fallback for environments where execCommand might fail
      const text = textarea.value;
      const newValue = text.substring(0, rangeStart) + replacement + text.substring(rangeEnd);
      textarea.value = newValue;
      onInput({ target: { value: newValue } });
    }

    textarea.setSelectionRange(selectStart, selectEnd);
  };

  const handleCopyCut = (e) => {
    const textarea = e.target;
    const { selectionStart, selectionEnd, value: text } = textarea;
    if (selectionStart === selectionEnd) {
      const { start, end, line } = getLineInfo(text, selectionStart, selectionEnd);
      let content = line;
      // Ensure it ends with a newline for block paste
      if (!content.endsWith("\n")) content += "\n";

      e.clipboardData.setData("text/plain", content);
      e.clipboardData.setData(mtKey, JSON.stringify({ defaultPastePayload: { pasteOnNewLine: true } }));
      e.preventDefault();

      if (e.type === "cut") {
        const nextChar = end < text.length ? 1 : 0;
        update(start, end + nextChar, "", start, start);
      }
    }
  };

  const handlePaste = (e) => {
    const copymetadata = getCopyMetadata(e.clipboardData);
    const isLineMode = copymetadata?.defaultPastePayload?.pasteOnNewLine;
    if (isLineMode) {
      e.preventDefault();
      const content = e.clipboardData.getData("text/plain");
      const textarea = e.target;
      const { selectionStart, selectionEnd, value: text } = textarea;
      const { start } = getLineInfo(text, selectionStart, selectionEnd);
      
      update(start, start, content, selectionStart + content.length, selectionEnd + content.length);
    }
  };

  const handleKeyDown = (e) => {
    const textarea = e.target;
    const { selectionStart, selectionEnd, value: text } = textarea;

    // Indentation & Tab
    if (e.code === "Tab") {
      e.preventDefault();
      let effectiveEnd = selectionEnd;
      const lineEnd = text[selectionEnd - 1] === "\n";
      if (selectionEnd > selectionStart && lineEnd) effectiveEnd--;
      
      const { start, end, line } = getLineInfo(text, selectionStart, effectiveEnd);
      const lines = line.split("\n");

      if (e.shiftKey) {
        // Dedent
        const newLines = lines.map(l => l.startsWith("  ") ? l.substring(2) : l.startsWith(" ") ? l.substring(1) : l);
        const replacement = newLines.join("\n");
        const diff = line.length - replacement.length;
        
        const shiftStart = (lines[0].startsWith(" ") ? (lines[0].startsWith("  ") ? 2 : 1) : 0);
        const newSelectionStart = Math.max(start, selectionStart - shiftStart);
        const newSelectionEnd = Math.max(newSelectionStart, selectionEnd - diff);
        
        update(start, end, replacement, newSelectionStart, newSelectionEnd);
      } else {
        // Indent
        if (selectionStart !== selectionEnd && (lines.length > 1 || lineEnd)) {
          const replacement = lines.map(l => "  " + l).join("\n");
          update(start, end, replacement, selectionStart + 2, selectionEnd + (lines.length * 2));
        } else {
          // Single cursor
          update(selectionStart, selectionEnd, "  ", selectionStart + 2, selectionStart + 2);
        }
      }
    }

    // Auto-indent on Enter
    if (e.code === "Enter") {
      e.preventDefault();
      const beforeCursor = text.substring(0, selectionStart);
      const afterCursor = text.substring(selectionEnd);
      const lineText = beforeCursor.split("\n").pop();
      const spaces = lineText.match(/^\s*/)[0];
      const replacement = "\n" + spaces;
      update(selectionStart, selectionEnd, replacement, selectionStart + replacement.length, selectionStart + replacement.length);
    }

    // Smart Backspace
    if (e.code === "Backspace" && selectionStart === selectionEnd) {
      const { start } = getLineInfo(text, selectionStart, selectionEnd);
      const beforeCursor = text.substring(start, selectionStart);
      if (beforeCursor.length > 0 && beforeCursor.trim() === "") {
        e.preventDefault();
        const removeCount = beforeCursor.length % 2 || 2;
        update(selectionStart - removeCount, selectionStart, "", selectionStart - removeCount, selectionStart - removeCount);
      }
    }

    // Move Lines (Alt + Up/Down)
    if (e.altKey && (e.code === "ArrowUp" || e.code === "ArrowDown")) {
      e.preventDefault();
      const { start, end, line } = getLineInfo(text, selectionStart, selectionEnd);

      if (e.code === "ArrowUp" && start > 0) {
        const prevStart = text.lastIndexOf("\n", start - 2) + 1;
        const prevLine = text.substring(prevStart, start - 1);
        const replacement = line + "\n" + prevLine;
        update(prevStart, end, replacement, selectionStart - (prevLine.length + 1), selectionEnd - (prevLine.length + 1));
      } else if (e.code === "ArrowDown" && end < text.length) {
        const nextEnd = text.indexOf("\n", end + 1);
        const actualNextEnd = nextEnd === -1 ? text.length : nextEnd;
        const nextLine = text.substring(end + 1, actualNextEnd);
        const replacement = nextLine + "\n" + line;
        update(start, actualNextEnd, replacement, selectionStart + (nextLine.length + 1), selectionEnd + (nextLine.length + 1));
      }
    }

    // Toggle Comment (Ctrl + /)
    if ((e.ctrlKey || e.metaKey) && e.code === "Slash") {
      e.preventDefault();
      let effectiveEnd = selectionEnd;
      if (selectionEnd > selectionStart && text[selectionEnd - 1] === "\n") effectiveEnd--;

      const { start, end, line } = getLineInfo(text, selectionStart, effectiveEnd);
      const lines = line.split("\n");

      const allCommented = lines.every(l => l.trim() === "" || l.trim().startsWith("--"));
      const newLines = lines.map(l => (l.trim() === "") ? l : (allCommented ? l.replace(/--+ ?/, "") : "-- " + l));
      
      const replacement = newLines.join("\n");
      const totalDiff = replacement.length - line.length;
      const firstLineDiff = newLines[0].length - lines[0].length;
      
      const newSelectionStart = Math.max(start, selectionStart + firstLineDiff);
      const newSelectionEnd = Math.max(newSelectionStart, selectionEnd + totalDiff);
      
      update(start, end, replacement, newSelectionStart, newSelectionEnd);
    }

    // Toggle Word Wrap (Alt + Z)
    if (e.altKey && e.code === "KeyZ") {
      e.preventDefault();
      setWordWrap(!wordWrap);
    }
  };

  return (
    <textarea
      ref={textareaRef}
      class={`${className} script-src`}
      style={{ whiteSpace: wordWrap ? "pre-wrap" : "pre" }}
      value={value}
      onInput={onInput}
      onKeyDown={handleKeyDown}
      onCopy={handleCopyCut}
      onCut={handleCopyCut}
      onPaste={handlePaste}
      spellcheck={false}
      rows={rows}
      placeholder={placeholder}
    />
  );
}
