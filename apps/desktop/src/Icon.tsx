import type { CSSProperties } from "react";
const paths = {
  network: <><rect x="9" y="3" width="6" height="6" rx="1.5"/><rect x="2" y="15" width="6" height="6" rx="1.5"/><rect x="16" y="15" width="6" height="6" rx="1.5"/><path d="M12 9v3M5 15v-3h14v3"/></>,
  files: <><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M8 13h8M8 17h5"/></>,
  chip: <><rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m-4 6h4m12-6h4m-4 6h4"/><rect x="10" y="10" width="4" height="4" rx=".5"/></>,
  check: <><path d="m8 12 3 3 5-6"/><path d="m12 2 8 4v6c0 5-8 10-8 10S4 17 4 12V6z"/></>,
  upload: <><path d="M12 16V3m-5 5 5-5 5 5M4 15v5h16v-5"/></>,
  arrow: <path d="M4 12h16m-6-6 6 6-6 6"/>,
  search: <><circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/></>,
  pause: <><path d="M8 5v14M16 5v14"/></>,
  play: <path d="m7 4 14 8-14 8z"/>,
  close: <path d="m6 6 12 12M6 18 18 6"/>,
  warning: <><path d="m12 3 10 18H2zM12 9v5"/><path d="M12 17h.01"/></>,
  folder: <path d="M3 6h7l2 3h9v11H3z"/>,
  link: <><path d="m9 15 6-6M8 17l-1 1a4 4 0 0 1-6-6l5-5a4 4 0 0 1 6 0m0 10a4 4 0 0 0 6 0l5-5a4 4 0 0 0-6-6l-1 1"/></>,
} as const;
export function Icon({ name, size = 20, style }: { name: keyof typeof paths; size?: number; style?: CSSProperties }) {
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" style={style}>{paths[name]}</svg>;
}
