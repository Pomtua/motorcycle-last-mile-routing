'use client';

import { useEffect } from 'react';

interface ModalProps {
  title: string;
  onClose: () => void;
  children: React.ReactNode;
  footer?: React.ReactNode;
  width?: string;
}

export default function Modal({ title, onClose, children, footer, width = 'max-w-lg' }: ModalProps) {
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => event.key === 'Escape' && onClose();
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  return (
    <div
      className="fixed inset-0 z-[2000] flex items-center justify-center bg-ink/40 p-4"
      onMouseDown={(event) => event.target === event.currentTarget && onClose()}
    >
      <div className={`w-full ${width} rounded-xl bg-white shadow-xl ring-1 ring-line`} role="dialog" aria-modal="true">
        <div className="flex items-center justify-between border-b border-line px-5 py-4">
          <h2 className="text-base font-semibold text-ink">{title}</h2>
          <button onClick={onClose} className="rounded-md p-1 text-muted hover:bg-zebra hover:text-ink" aria-label="Close">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M6 6l12 12M18 6L6 18" />
            </svg>
          </button>
        </div>
        <div className="px-5 py-4">{children}</div>
        <div className="flex justify-end gap-2 border-t border-line px-5 py-3">
          {footer ?? (
            <button onClick={onClose} className="rounded-lg bg-primary px-4 py-2 text-sm font-semibold text-white">
              OK
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
