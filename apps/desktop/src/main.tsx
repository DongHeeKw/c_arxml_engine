import { StrictMode, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import "./style.css";

type EngineStatus = {
  engineVersion: string;
  protocolVersion: string;
  supportedMethods: string[];
};

type DbcSignal = {
  bitLength: number;
  byteOrder: string;
  name: string;
  startBit: number;
  unit: string;
};

type DbcMessage = {
  dlcBytes: number;
  frameId: number;
  line: number;
  name: string;
  signals: DbcSignal[];
  transmitter: string;
};

type DbcPreview = {
  issues: { code: string; line: number; message: string; severity: string }[];
  messages: DbcMessage[];
  nodes: string[];
  unsupportedLines: number;
};

const sampleDbc = `VERSION "1.0"

BU_: ECU_A ECU_B

BO_ 291 Status: 8 ECU_A
 SG_ Speed : 0|16@1+ (0.1,0) [0|250] "km/h" ECU_B
 SG_ Enabled : 16|1@1+ (1,0) [0|1] "" ECU_B
`;

function App() {
  const [engine, setEngine] = useState<EngineStatus | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [dbcText, setDbcText] = useState(sampleDbc);
  const [preview, setPreview] = useState<DbcPreview | null>(null);
  const [parseError, setParseError] = useState<string | null>(null);

  useEffect(() => {
    if (!window.studio?.request) {
      setError("Desktop bridge unavailable. Start the app with npm run dev.");
      return;
    }
    window.studio.request<EngineStatus>("initialize")
      .then(setEngine)
      .catch((cause: unknown) => {
        setError(cause instanceof Error ? cause.message : String(cause));
      });
  }, []);

  async function parseDbc() {
    setParseError(null);
    setPreview(null);
    try {
      const result = await window.studio.request<DbcPreview>("dbc.parse", { text: dbcText });
      setPreview(result);
    } catch (cause) {
      setParseError(cause instanceof Error ? cause.message : String(cause));
    }
  }

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand-mark">AS</div>
        <div>
          <p className="eyebrow">AUTOSAR DEPENDENCY-DRIVEN STUDIO</p>
          <h1>Project workspace</h1>
        </div>
        <div className="connection" role="status">
          <span className={`status-dot ${error ? "offline" : engine ? "online" : ""}`} />
          {error ? "Engine unavailable" : engine ? "Engine connected" : "Connecting"}
        </div>
      </header>

      <section className="welcome">
        <p className="eyebrow">START HERE</p>
        <h2>Bring your network configuration into focus.</h2>
        <p className="description">
          Preview a DBC in the C++ engine. Project persistence and full-network import are next.
        </p>
      </section>

      <section className="status-card" aria-live="polite">
        <div>
          <p className="card-label">ENGINE STATUS</p>
          {error ? (
            <p className="error-message">{error}</p>
          ) : engine ? (
            <p className="status-message">
              C++ engine {engine.engineVersion} · JSON-RPC {engine.protocolVersion}
            </p>
          ) : (
            <p className="status-message">Waiting for the local engine…</p>
          )}
        </div>
        <span className="card-icon" aria-hidden="true">C++</span>
      </section>

      <section className="dbc-panel">
        <div className="panel-heading">
          <div>
            <p className="card-label">ENGINE PREVIEW</p>
            <h2>DBC declarations</h2>
          </div>
          <button
            type="button"
            className="primary-button"
            disabled={!engine?.supportedMethods.includes("dbc.parse")}
            onClick={() => void parseDbc()}
          >
            Parse sample
          </button>
        </div>
        <label className="editor-label" htmlFor="dbc-input">DBC source</label>
        <textarea
          id="dbc-input"
          spellCheck={false}
          value={dbcText}
          onChange={(event) => setDbcText(event.target.value)}
        />
        {parseError && <p className="error-message">{parseError}</p>}
        {preview && (
          <div className="preview-results" aria-live="polite">
            <div className="preview-summary">
              <span>{preview.nodes.length} nodes</span>
              <span>{preview.messages.length} messages</span>
              <span>{preview.issues.length} issues</span>
            </div>
            {preview.messages.map((message) => (
              <article className="message-card" key={`${message.frameId}-${message.line}`}>
                <div className="message-heading">
                  <strong>{message.name}</strong>
                  <span>ID 0x{message.frameId.toString(16).toUpperCase()} · {message.dlcBytes} bytes</span>
                </div>
                <p>Transmitter: {message.transmitter}</p>
                <ul>
                  {message.signals.map((signal) => (
                    <li key={signal.name}>
                      {signal.name} · bit {signal.startBit}, {signal.bitLength} bits · {signal.byteOrder}
                      {signal.unit && ` · ${signal.unit}`}
                    </li>
                  ))}
                </ul>
              </article>
            ))}
            {preview.issues.map((issue, index) => (
              <p className={issue.severity === "error" ? "error-message" : "issue-message"} key={`${issue.code}-${index}`}>
                {issue.severity.toUpperCase()} · line {issue.line} · {issue.code}: {issue.message}
              </p>
            ))}
            {preview.unsupportedLines > 0 && (
              <p className="issue-message">
                {preview.unsupportedLines} unrecognized source lines were not interpreted.
              </p>
            )}
          </div>
        )}
      </section>

      <footer>
        <span>Local-first configuration workspace</span>
        <span>{engine ? `${engine.supportedMethods.length} engine methods available` : "Development baseline"}</span>
      </footer>
    </main>
  );
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
