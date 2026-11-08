import { StrictMode, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import "./style.css";

type EngineStatus = {
  engineVersion: string;
  protocolVersion: string;
  supportedMethods: string[];
};

function App() {
  const [engine, setEngine] = useState<EngineStatus | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    window.studio.request<EngineStatus>("initialize")
      .then(setEngine)
      .catch((cause: unknown) => {
        setError(cause instanceof Error ? cause.message : String(cause));
      });
  }, []);

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
          Create or open a project, then import DBC files to explore ECU communication and validation.
        </p>
        <button type="button" disabled>Open project <span>Coming next</span></button>
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
        <span className="card-icon" aria-hidden="true">⌘</span>
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
