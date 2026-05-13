import { useEffect } from "preact/hooks";

/**
 * Reusable Card component using Pico's <article>
 */
export function Card({ title, children, footer, notice, btn, class: className }) {
  return (
    <article class={className}>
      {title && <header><strong>{title}</strong>{btn}</header>}
      {notice && <p class="notice">{notice}</p>}
      {children}
      {footer && <footer>{footer}</footer>}
    </article>
  );
}

/**
 * Input field with shaded description (tooltip style)
 */
export function FormField({ label, description, children }) {
  return (
    <div class="field">
      <label style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '0.2rem' }}>
        {label}
      </label>
      {description && (
        <div class="description">
          {description}
        </div>
      )}
      {children}
    </div>
  );
}

/**
 * Basic Modal component
 */
export function Modal({ isOpen, title, children, onClose, onConfirm, confirmText = "Confirm", footer, class: className }) {
  useEffect(() => {
    if (!isOpen) return;

    const handleKeyDown = (e) => {
      if (e.key === "Escape") {
        onClose();
      }
    };

    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [isOpen, onClose]);

  if (!isOpen) return null;

  return (
    <dialog open class={className}>
      <article>
        <header>
          <a href="#close" aria-label="Close" class="close" onClick={onClose}></a>
          {title}
        </header>
        {children}
        <footer>
          {footer ? footer : (
            <>
              <button class="secondary outline" onClick={onClose}>{ onConfirm ? 'Cancel' : 'Close' }</button>
              {onConfirm && <button onClick={onConfirm}>{confirmText}</button>}
            </>
          )}
        </footer>
      </article>
    </dialog>
  );
}
