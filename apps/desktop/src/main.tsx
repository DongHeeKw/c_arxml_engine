import { StrictMode, useEffect, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import "./style.css";

type EngineStatus = {
  engineVersion: string;
  protocolVersion: string;
  supportedMethods: string[];
};

type TaskTrigger = "periodic" | "event" | "isr";

type OsTaskDraft = {
  name: string;
  trigger: TaskTrigger;
  periodMs: string;
  event: string;
  isrVector: string;
};

type DcmServiceDraft = {
  serviceId: string;
  name: string;
  enabled: boolean;
};

type RequirementDraft = {
  id: string;
  text: string;
};

type SrsIssue = {
  code: string;
  message: string;
  path: string;
  severity: string;
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

type DbcBatchFinding = {
  code: string;
  identity: string;
  key: string;
  message: string;
  networkScope: string;
  severity: string;
  sourceFiles: string[];
  sourceIndices: number[];
};

type ImportedDbc = {
  name: string;
  preview?: DbcPreview;
  error?: string;
};

type ProjectDefinition = {
  name: string;
  mcu: { vendor: string; family: string; partNumber: string };
  dbcSources: string[];
  tasks: { name: string; trigger: TaskTrigger; periodMs: number | null; event: string; isrVector: string }[];
  dcmServices: { serviceId: number; name: string; enabled: boolean }[];
  requirements: RequirementDraft[];
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
  const [imports, setImports] = useState<ImportedDbc[]>([]);
  const [batchFindings, setBatchFindings] = useState<DbcBatchFinding[]>([]);
  const [importError, setImportError] = useState<string | null>(null);
  const [isImporting, setIsImporting] = useState(false);
  const [isDragging, setIsDragging] = useState(false);
  const [projectName, setProjectName] = useState("");
  const [mcuVendor, setMcuVendor] = useState("");
  const [mcuFamily, setMcuFamily] = useState("");
  const [mcuPartNumber, setMcuPartNumber] = useState("");
  const [tasks, setTasks] = useState<OsTaskDraft[]>([]);
  const [dcmServices, setDcmServices] = useState<DcmServiceDraft[]>([]);
  const [requirements, setRequirements] = useState<RequirementDraft[]>([]);
  const [projectDraft, setProjectDraft] = useState<ProjectDefinition | null>(null);
  const [srsIssues, setSrsIssues] = useState<SrsIssue[]>([]);
  const [projectSaveError, setProjectSaveError] = useState<string | null>(null);
  const [isSavingProject, setIsSavingProject] = useState(false);
  const fileInput = useRef<HTMLInputElement>(null);

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

  async function parseFiles(files: { name: string; text: string }[]) {
    if (files.length === 0) return;
    setImportError(null);
    setIsImporting(true);
    setImports([]);
    setBatchFindings([]);
    try {
      const results = await window.studio.request<{
        files: { name: string; preview: DbcPreview }[];
        findings: DbcBatchFinding[];
      }>(
        "dbc.parseBatch",
        { files },
      );
      setImports(results.files.map(({ name, preview }) => ({ name, preview })));
      setBatchFindings(results.findings);
    } catch (cause) {
      setImportError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      setIsImporting(false);
    }
  }

  async function configureProject() {
    setProjectSaveError(null);
    setSrsIssues([]);
    setIsSavingProject(true);
    try {
      const services = dcmServices.map((service) => {
        const serviceId = Number.parseInt(service.serviceId.trim().replace(/^0x/i, ""), 16);
        if (!Number.isInteger(serviceId) || serviceId < 0 || serviceId > 0xff) {
          throw new Error(`Invalid DCM service ID: ${service.serviceId || "(empty)"}`);
        }
        return { ...service, serviceId };
      });
      const result = await window.studio.request<{
        accepted: boolean;
        issues: SrsIssue[];
        project?: ProjectDefinition;
      }>("project.configure", {
        name: projectName,
        mcu: { vendor: mcuVendor, family: mcuFamily, partNumber: mcuPartNumber },
        dbcSources: imports.filter((item) => item.preview).map((item) => item.name),
        tasks: tasks.map((task) => ({
          name: task.name,
          trigger: task.trigger,
          periodMs: task.trigger === "periodic" && task.periodMs ? Number(task.periodMs) : null,
          event: task.trigger === "event" ? task.event : "",
          isrVector: task.trigger === "isr" ? task.isrVector : "",
        })),
        dcmServices: services,
        requirements,
      });
      setSrsIssues(result.issues);
      if (result.accepted && result.project) setProjectDraft(result.project);
    } catch (cause) {
      setProjectSaveError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      setIsSavingProject(false);
    }
  }

  function updateTask(index: number, patch: Partial<OsTaskDraft>) {
    setTasks((current) => current.map((task, itemIndex) =>
      itemIndex === index ? { ...task, ...patch } : task));
  }

  function updateService(index: number, patch: Partial<DcmServiceDraft>) {
    setDcmServices((current) => current.map((service, itemIndex) =>
      itemIndex === index ? { ...service, ...patch } : service));
  }

  function updateRequirement(index: number, patch: Partial<RequirementDraft>) {
    setRequirements((current) => current.map((requirement, itemIndex) =>
      itemIndex === index ? { ...requirement, ...patch } : requirement));
  }

  async function chooseDbcFiles() {
    if (!engine) {
      setImportError("Connect to the C++ engine before importing DBC files.");
      return;
    }
    setImportError(null);
    try {
      if (window.studio?.openDbcFiles) {
        const files = await window.studio.openDbcFiles();
        await parseFiles(files);
      } else {
        fileInput.current?.click();
      }
    } catch (cause) {
      setImportError(cause instanceof Error ? cause.message : String(cause));
    }
  }

  async function readBrowserFiles(fileList: FileList | null) {
    if (!fileList || fileList.length === 0) return;
    try {
      const files = await Promise.all(Array.from(fileList, async (file) => {
        if (!file.name.toLowerCase().endsWith(".dbc")) {
          throw new Error(`${file.name} is not a .dbc file`);
        }
        if (file.size > 900 * 1024) {
          throw new Error(`${file.name} exceeds the 900 KiB import preview limit`);
        }
        return { name: file.name, text: await file.text() };
      }));
      await parseFiles(files);
    } catch (cause) {
      setImportError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      if (fileInput.current) fileInput.current.value = "";
    }
  }

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand-mark" aria-hidden="true">AS</div>
        <div className="brand-copy">
          <h1>AUTOSAR Studio</h1>
          <p>Configuration workspace</p>
        </div>
        <div className="project-indicator">
          <span className="project-indicator-label">PROJECT DEFINITION</span>
          <span>{projectDraft?.name ?? "Not defined"}</span>
        </div>
        <div className="connection" role="status">
          <span className={`status-dot ${error ? "offline" : engine ? "online" : ""}`} />
          <span>{error ? "Engine unavailable" : engine ? "Engine connected" : "Connecting"}</span>
        </div>
      </header>

      <section className="page-heading">
        <div>
          <p className="eyebrow">DBC + SOFTWARE REQUIREMENTS</p>
          <h2>ECU project workspace</h2>
          <p>Describe the ECU target, network, OS timing, and diagnostic services in one project definition.</p>
        </div>
        <span className="mode-label">SESSION DRAFT</span>
      </section>

      <div className="workspace-grid">
        <section className="main-column">
          <section className="dbc-panel">
            <div className="panel-heading">
              <div>
                <h3>DBC files</h3>
                <p>One or more files · preview before project import</p>
              </div>
              <button
                type="button"
                className="primary-button"
                disabled={!engine?.supportedMethods.includes("dbc.parseBatch") || isImporting}
                onClick={() => void chooseDbcFiles()}
              >
                {isImporting ? "Checking files…" : "Add DBC files"}
              </button>
            </div>
            <input
              ref={fileInput}
              className="visually-hidden"
              type="file"
              accept=".dbc"
              multiple
              onChange={(event) => void readBrowserFiles(event.currentTarget.files)}
            />
            <div
              className={`drop-zone${isDragging ? " is-dragging" : ""}`}
              onDragEnter={(event) => {
                event.preventDefault();
                setIsDragging(true);
              }}
              onDragOver={(event) => event.preventDefault()}
              onDragLeave={(event) => {
                if (!event.currentTarget.contains(event.relatedTarget as Node | null)) {
                  setIsDragging(false);
                }
              }}
              onDrop={(event) => {
                event.preventDefault();
                setIsDragging(false);
                void readBrowserFiles(event.dataTransfer.files);
              }}
            >
              <span className="drop-icon" aria-hidden="true">↑</span>
              <div>
                <strong>Drop DBC files here</strong>
                <p>We’ll check their nodes, messages, and signals before anything is saved.</p>
              </div>
            </div>
            {importError && <p className="error-message" role="alert">{importError}</p>}
            {!engine && (
              <p className="hint-message">
                {error ?? "Waiting for the local C++ engine before files can be parsed."}
              </p>
            )}
            {imports.length > 0 && (
              <div className="preview-results" aria-live="polite">
                <div className="preview-summary">
                  <h3>Import preview</h3>
                  <span>{imports.length} file{imports.length === 1 ? "" : "s"}</span>
                  <span>{imports.reduce((count, item) => count + (item.preview?.nodes.length ?? 0), 0)} nodes</span>
                  <span>{imports.reduce((count, item) => count + (item.preview?.messages.length ?? 0), 0)} messages</span>
                </div>
                {batchFindings.length > 0 && (
                  <section className="batch-findings" aria-label="Cross-file identity review">
                    <h4>Possible cross-file overlaps</h4>
                    <p className="hint-message">
                      Network assignment is not available yet. Treat these as unresolved review
                      prompts, not confirmed conflicts.
                    </p>
                    {batchFindings.map((finding, index) => (
                      <p className="issue-message" key={`${finding.code}-${finding.key}-${index}`}>
                        {finding.severity.toUpperCase()} · {finding.code}: {finding.message}
                        <br />
                        {finding.identity} {finding.key} · {finding.sourceFiles.map((name, sourceIndex) =>
                          `#${finding.sourceIndices[sourceIndex]} ${name}`).join(", ")}
                      </p>
                    ))}
                  </section>
                )}
                {imports.map((item) => (
                  <article className="file-card" key={item.name}>
                    <div className="message-heading">
                      <strong>{item.name}</strong>
                      {item.preview && (
                        <span>{item.preview.nodes.length} nodes · {item.preview.messages.length} messages · {item.preview.issues.length} issues</span>
                      )}
                    </div>
                    {item.error && <p className="error-message">{item.error}</p>}
                    {item.preview?.messages.map((message) => (
                      <div className="message-card" key={`${message.frameId}-${message.line}`}>
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
                      </div>
                    ))}
                    {item.preview?.issues.map((issue, index) => (
                      <p className={issue.severity === "error" ? "error-message" : "issue-message"} key={`${issue.code}-${index}`}>
                        {issue.severity.toUpperCase()} · line {issue.line} · {issue.code}: {issue.message}
                      </p>
                    ))}
                    {(item.preview?.unsupportedLines ?? 0) > 0 && (
                      <p className="issue-message">
                        {item.preview?.unsupportedLines} unsupported declarations or unrecognized source lines were not interpreted.
                      </p>
                    )}
                  </article>
                ))}
              </div>
            )}
            <details className="sample-details">
              <summary>Use sample DBC text instead</summary>
              <label className="editor-label" htmlFor="dbc-input">Sample source editor</label>
              <textarea
                id="dbc-input"
                spellCheck={false}
                value={dbcText}
                onChange={(event) => setDbcText(event.target.value)}
              />
              <button
                type="button"
                className="text-button sample-button"
                disabled={!engine?.supportedMethods.includes("dbc.parseBatch") || isImporting}
                onClick={() => void parseFiles([{ name: "sample.dbc", text: dbcText }])}
              >
                Preview sample
              </button>
            </details>
          </section>

          <section className="srs-panel">
            <div className="panel-heading">
              <div>
                <h3>Project definition</h3>
                <p>Define the ECU target and software requirements alongside imported DBC files.</p>
              </div>
              <span className="mode-label">SESSION DRAFT</span>
            </div>

            <div className="srs-section">
              <h4>Project and MCU</h4>
              <div className="form-grid">
                <label className="form-field">
                  <span>Project name</span>
                  <input value={projectName} onChange={(event) => setProjectName(event.target.value)} placeholder="Body Controller" />
                </label>
                <label className="form-field">
                  <span>MCU vendor</span>
                  <input value={mcuVendor} onChange={(event) => setMcuVendor(event.target.value)} placeholder="Vendor" />
                </label>
                <label className="form-field">
                  <span>MCU family</span>
                  <input value={mcuFamily} onChange={(event) => setMcuFamily(event.target.value)} placeholder="Family" />
                </label>
                <label className="form-field">
                  <span>MCU part number</span>
                  <input value={mcuPartNumber} onChange={(event) => setMcuPartNumber(event.target.value)} placeholder="Exact target part" />
                </label>
              </div>
            </div>

            <div className="srs-section">
              <div className="subsection-heading">
                <h4>OS task requirements</h4>
                <button
                  type="button"
                  className="text-button"
                  onClick={() => setTasks((current) => [
                    ...current,
                    { name: "", trigger: "periodic", periodMs: "10", event: "", isrVector: "" },
                  ])}
                >
                  Add task
                </button>
              </div>
              {tasks.length === 0 && <p className="empty-note">Add periodic, event-triggered, or ISR task requirements.</p>}
              {tasks.map((task, index) => (
                <div className="srs-row" key={`task-${index}`}>
                  <div className="form-grid task-grid">
                    <label className="form-field">
                      <span>Task name</span>
                      <input value={task.name} onChange={(event) => updateTask(index, { name: event.target.value })} placeholder="Com_MainFunction" />
                    </label>
                    <label className="form-field">
                      <span>Activation</span>
                      <select value={task.trigger} onChange={(event) => updateTask(index, { trigger: event.target.value as TaskTrigger })}>
                        <option value="periodic">Periodic / polling</option>
                        <option value="event">Event</option>
                        <option value="isr">ISR</option>
                      </select>
                    </label>
                    {task.trigger === "periodic" && (
                      <label className="form-field">
                        <span>Period (ms)</span>
                        <input type="number" min="1" max="60000" value={task.periodMs} onChange={(event) => updateTask(index, { periodMs: event.target.value })} placeholder="5 or 10" />
                      </label>
                    )}
                    {task.trigger === "event" && (
                      <label className="form-field">
                        <span>Event name</span>
                        <input value={task.event} onChange={(event) => updateTask(index, { event: event.target.value })} placeholder="CanIfRxIndication" />
                      </label>
                    )}
                    {task.trigger === "isr" && (
                      <label className="form-field">
                        <span>ISR vector</span>
                        <input value={task.isrVector} onChange={(event) => updateTask(index, { isrVector: event.target.value })} placeholder="CAN0_IRQHandler" />
                      </label>
                    )}
                  </div>
                  <button type="button" className="remove-button" onClick={() => setTasks((current) => current.filter((_, itemIndex) => itemIndex !== index))}>
                    Remove task
                  </button>
                </div>
              ))}
            </div>

            <div className="srs-section">
              <div className="subsection-heading">
                <h4>DCM service requirements</h4>
                <button
                  type="button"
                  className="text-button"
                  onClick={() => setDcmServices((current) => [
                    ...current,
                    { serviceId: "", name: "", enabled: true },
                  ])}
                >
                  Add service
                </button>
              </div>
              {dcmServices.length === 0 && <p className="empty-note">Record diagnostic services required by this project.</p>}
              {dcmServices.map((service, index) => (
                <div className="srs-row compact-row" key={`service-${index}`}>
                  <label className="form-field service-id-field">
                    <span>Service ID (hex)</span>
                    <input value={service.serviceId} onChange={(event) => updateService(index, { serviceId: event.target.value })} placeholder="0x22" />
                  </label>
                  <label className="form-field">
                    <span>Service name</span>
                    <input value={service.name} onChange={(event) => updateService(index, { name: event.target.value })} placeholder="ReadDataByIdentifier" />
                  </label>
                  <label className="checkbox-field">
                    <input type="checkbox" checked={service.enabled} onChange={(event) => updateService(index, { enabled: event.target.checked })} />
                    <span>Required</span>
                  </label>
                  <button type="button" className="remove-button" onClick={() => setDcmServices((current) => current.filter((_, itemIndex) => itemIndex !== index))}>
                    Remove
                  </button>
                </div>
              ))}
            </div>

            <div className="srs-section">
              <div className="subsection-heading">
                <h4>Software requirements</h4>
                <button
                  type="button"
                  className="text-button"
                  onClick={() => setRequirements((current) => [...current, { id: "", text: "" }])}
                >
                  Add requirement
                </button>
              </div>
              {requirements.map((requirement, index) => (
                <div className="srs-row requirement-row" key={`requirement-${index}`}>
                  <label className="form-field requirement-id-field">
                    <span>Requirement ID</span>
                    <input value={requirement.id} onChange={(event) => updateRequirement(index, { id: event.target.value })} placeholder="SRS-COM-001" />
                  </label>
                  <label className="form-field">
                    <span>Requirement</span>
                    <input value={requirement.text} onChange={(event) => updateRequirement(index, { text: event.target.value })} placeholder="Describe expected ECU behavior" />
                  </label>
                  <button type="button" className="remove-button" onClick={() => setRequirements((current) => current.filter((_, itemIndex) => itemIndex !== index))}>
                    Remove
                  </button>
                </div>
              ))}
            </div>

            <div className="srs-actions">
              <button
                type="button"
                className="primary-button"
                disabled={!engine?.supportedMethods.includes("project.configure") || isSavingProject}
                onClick={() => void configureProject()}
              >
                {isSavingProject ? "Validating…" : "Validate project definition"}
              </button>
              {projectDraft && <span>Validated in this engine session</span>}
            </div>
            {projectSaveError && <p className="error-message" role="alert">{projectSaveError}</p>}
            {srsIssues.map((issue, index) => (
              <p className={issue.severity === "error" ? "error-message" : "issue-message"} key={`${issue.code}-${index}`}>
                {issue.severity.toUpperCase()} · {issue.path} · {issue.message}
              </p>
            ))}
          </section>
        </section>

        <aside className="side-column">
          <section className="info-card">
            <p className="section-label">PROJECT DEFINITION</p>
            <h3>{projectDraft?.name ?? "Not defined yet"}</h3>
            <p>{projectDraft ? "The current session has a validated DBC and SRS draft." : "Create a session draft from DBC previews and project software requirements."}</p>
            {projectDraft?.mcu.partNumber && <p>MCU · {projectDraft.mcu.partNumber}</p>}
            {projectDraft && <p>{projectDraft.tasks.length} OS tasks · {projectDraft.dcmServices.length} DCM services · {projectDraft.requirements.length} requirements</p>}
            <div className="project-files">
              {projectDraft?.dbcSources.length
                ? projectDraft.dbcSources.map((source) => <span key={source}>{source}</span>)
                : <span>DBC not imported</span>}
            </div>
          </section>
          <section className="info-card">
            <p className="section-label">PREVIEW CONTENT</p>
            <ul className="feature-list">
              <li>ECU node declarations</li>
              <li>CAN message IDs and transmitters</li>
              <li>Signal layout, scaling, and receivers</li>
              <li>Parser issues with source lines</li>
            </ul>
          </section>
          <section className="notice-card">
            <strong>Session draft only</strong>
            <p>DBC previews and SRS settings are not persisted to SQLite or an Eclipse project yet.</p>
          </section>
        </aside>
      </div>

      <footer>
        <span>Configuration, generation, and build remain separate workflows.</span>
        <span>{engine ? `Engine ${engine.engineVersion}` : "Engine not connected"}</span>
      </footer>
    </main>
  );
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
