import { useEffect, useState } from "react";
import { Icon } from "./Icon";

export type NetworkMessage = { sourceId: number; sourceUri: string; name: string; canId: number; extended: boolean;
  dlcBytes: number; direction: string; line: number; transmitter: string; transmitterDeclared: boolean;
  receivers: string[]; peers: { name: string; direction: "Tx" | "Rx" }[]; signals: { name: string; line: number }[] };
export type EcuView = { messages: NetworkMessage[] };
export type SavedSource = { sourceId: number; sourceUri: string; name: string; channel: string; nodes: string[]; revision: number;
  issues: { code: string; line: number; severity: string; message: string }[]; unsupportedLines: number };

export function NetworkWorkspace({ sources, channel, ecu, view, error, onChannel, onEcu, onImport }: {
  sources: SavedSource[]; channel: string; ecu: string; view: EcuView | null; error: string | null;
  onChannel: (value: string) => void; onEcu: (value: string) => void; onImport: () => void;
}) {
  const [peer, setPeer] = useState("");
  const [filter, setFilter] = useState("");
  const [direction, setDirection] = useState("All");
  const [selected, setSelected] = useState<string | null>(null);
  const [motion, setMotion] = useState(true);
  useEffect(() => { setPeer(""); setSelected(null); setFilter(""); }, [channel, ecu]);
  const messages = view?.messages ?? [];
  const peers = [...new Set(messages.flatMap((message) => message.peers.map((item) => item.name)))].sort();
  const visible = messages.filter((message) => (!peer || message.peers.some((item) => item.name === peer)) &&
    (direction === "All" || message.direction.includes(direction)) &&
    `${message.name} ${message.canId.toString(16)} ${message.signals.map((signal) => signal.name).join(" ")}`.toLowerCase().includes(filter.toLowerCase()));
  const key = (message: NetworkMessage) => `${message.sourceId}/${message.name}`;
  const inspected = visible.find((message) => key(message) === selected) ?? visible[0];
  const nodes = [...new Set(sources.filter((source) => source.channel === channel).flatMap((source) => source.nodes))].filter((node) => node !== "Vector__XXX").sort();
  const shownPeers = (peer ? peers.filter((name) => name === peer) : peers.slice(0, 8));
  return <section className="network-workspace" aria-label="Saved ECU network">
    <div className="workspace-toolbar">
      <div className="toolbar-heading"><span className="icon-tile"><Icon name="network"/></span><div><h3>Network explorer</h3><p>One ECU. A clear view of every connection.</p></div></div>
      <div className="network-selectors">
        <label className="form-field"><span>Channel</span><select aria-label="Channel" value={channel} onChange={(event) => onChannel(event.target.value)}><option value="">Select channel</option>{[...new Set(sources.map((source) => source.channel))].sort().map((name) => <option key={name}>{name}</option>)}</select></label>
        <label className="form-field"><span>Target ECU</span><select aria-label="Target ECU" value={ecu} onChange={(event) => onEcu(event.target.value)}><option value="">Select ECU</option>{nodes.map((name) => <option key={name}>{name}</option>)}</select></label>
      </div>
    </div>
    <div className={`topology ${motion ? "motion-enabled" : ""}`} aria-label="ECU topology">
      <div className="canvas-label"><span className="tiny-dot"/> CONFIGURATION TOPOLOGY <span>Not live bus traffic</span></div>
      <button className="icon-button motion-toggle" type="button" aria-label={motion ? "Pause diagram motion" : "Resume diagram motion"} aria-pressed={!motion} onClick={() => setMotion(!motion)}><Icon name={motion ? "pause" : "play"} size={16}/></button>
      {view && ecu ? <>
        <svg className="topology-wires" viewBox="0 0 1000 420" preserveAspectRatio="none" aria-hidden="true">
          {shownPeers.map((name, index) => {
            const isTx = messages.some((message) => message.peers.some((item) => item.name === name && item.direction === "Tx"));
            const right = index % 2 === 1;
            const y = 92 + Math.floor(index / 2) * (shownPeers.length > 6 ? 78 : 112);
            const x = right ? 800 : 200;
            const d = `M500 210 C${right ? 680 : 320} 210 ${right ? 640 : 360} ${y} ${x} ${y}`;
            return <g key={name} className={`${peer && peer !== name ? "wire-muted" : ""} ${isTx ? "wire-tx" : "wire-rx"}`}><path d={d} className="wire-base"/><path d={d} className={`wire-flow ${isTx ? "" : "reverse"}`}/></g>;
          })}
        </svg>
        <div className="ecu-hub"><div className="hub-icon"><Icon name="chip" size={30}/></div><strong title={ecu}>{ecu}</strong><span>Selected ECU</span><div className="hub-stats">{messages.filter((item) => item.direction.includes("Tx")).length} Tx <span> / </span> {messages.filter((item) => item.direction.includes("Rx")).length} Rx</div></div>
        {shownPeers.map((name, index) => {
          const connected = messages.filter((message) => message.peers.some((item) => item.name === name));
          const dirs = [...new Set(connected.flatMap((message) => message.peers.filter((item) => item.name === name).map((item) => item.direction)))].sort().join(" / ");
          return <button key={name} type="button" className={`peer-node ${peer === name ? "selected" : ""}`} aria-label={`Show messages with ${name}`} aria-pressed={peer === name}
            style={{ left: index % 2 ? "80%" : "20%", top: `${(92 + Math.floor(index / 2) * (shownPeers.length > 6 ? 78 : 112)) / 4.2}%` }} onClick={() => { setPeer(peer === name ? "" : name); setSelected(null); }}>
            <Icon name="chip"/><span><strong title={name}>{name}</strong><small>{connected.length} messages · {dirs}</small></span><span className="peer-indicator"/>
          </button>;
        })}
        <div className="map-legend"><span><i className="legend-tx"/>Transmit</span><span><i className="legend-rx"/>Receive</span><span>{peers.length} connected ECUs</span></div>
      </> : <div className="topology-empty"><div className="empty-orbit"><span/><span/><Icon name="network" size={36}/></div><h4>{error ? "Network unavailable" : channel && ecu ? "Reading your network…" : sources.length ? "Choose your point of view" : "Your network starts here"}</h4><p>{error || (sources.length ? "Select a channel and target ECU to reveal its communication links." : "Import your DBC files to turn network definitions into a connected workspace.")}</p>{!sources.length && <button className="primary-button" onClick={onImport}><Icon name="upload" size={16}/>Import DBC files</button>}</div>}
    </div>
    {error && <p className="error-message network-error" role="alert">{error}</p>}
    {view && <div className="message-workbench">
      <div className="message-browser">
        <div className="message-browser-heading"><h3>Messages <span className="count-pill">{visible.length}</span></h3><select aria-label="Connected ECU" value={peer} onChange={(event) => { setPeer(event.target.value); setSelected(null); }}><option value="">All connections ({peers.length})</option>{peers.map((name) => <option key={name}>{name}</option>)}</select></div>
        <div className="message-tools"><label className="search-box"><Icon name="search" size={16}/><input aria-label="Search messages" value={filter} onChange={(event) => setFilter(event.target.value)} placeholder="Search name, CAN ID or signal…"/></label><div className="segmented" aria-label="Message direction">{["All", "Tx", "Rx"].map((value) => <button key={value} aria-pressed={direction === value} onClick={() => setDirection(value)}>{value}</button>)}</div></div>
        <div className="message-table" role="group" aria-label="ECU messages">
          {visible.map((message) => <button className={`message-row ${inspected && key(inspected) === key(message) ? "selected" : ""}`} key={key(message)} onClick={() => setSelected(key(message))} aria-label={`Inspect ${message.name}`}><span className={`direction-badge ${message.direction === "Rx" ? "rx" : "tx"}`}>{message.direction}</span><span className="message-title"><strong>{message.name}</strong><small>{message.signals.length} signals · {message.dlcBytes} bytes</small></span><code>0x{message.canId.toString(16).toUpperCase()}</code><Icon name="arrow" size={16}/></button>)}
          {!visible.length && <div className="empty-results">No matching messages.<button className="text-button" onClick={() => { setFilter(""); setDirection("All"); setPeer(""); }}>Clear filters</button></div>}
        </div>
      </div>
      <aside className="signal-inspector" aria-label="Message inspector">
        <p className="eyebrow">MESSAGE INSPECTOR</p>{inspected ? <><h3>{inspected.name}</h3><div className="inspector-tags"><span>{inspected.direction}</span><span>{inspected.extended ? "Extended ID" : "Standard ID"}</span><span>{inspected.dlcBytes} bytes</span></div><dl><dt>Transmitter</dt><dd>{inspected.transmitter}{!inspected.transmitterDeclared && " · unresolved"}</dd><dt>CAN identifier</dt><dd>0x{inspected.canId.toString(16).toUpperCase()}</dd><dt>Source</dt><dd className="source-path" title={inspected.sourceUri}>{inspected.sourceUri}</dd><dt>Source line</dt><dd>{inspected.line}</dd></dl><h4>Signals for {ecu}</h4><ul className="signal-list">{inspected.signals.map((signal) => <li key={signal.name}><span className="signal-mark"/><strong>{signal.name}</strong><small>line {signal.line}</small></li>)}</ul></> : <p>Select a message to inspect its source and signals.</p>}
      </aside>
    </div>}
  </section>;
}
