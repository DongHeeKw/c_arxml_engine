# AUTOSAR Studio — Architecture Acceptance & AI Implementation Guardrails

> **Purpose:** This is an executable-minded acceptance contract for Codex or another coding agent. It is **not** a project schedule. The agent must use it to assess the repository and prevent architectural drift. **Do not claim PASS without repository evidence and executed tests.**
>
> Status values: `PASS` (evidence attached), `FAIL` (contradiction/test failure), `NOT_IMPLEMENTED`, `BLOCKED` (missing legitimate input/tool), `NOT_APPLICABLE` (reason required). Unknown is never PASS.

## 0. Product invariant / 제품 불변조건

The product's primary job is **reduce Time to First ASW Build** while maintaining correctness and compatibility with the existing AUTOSAR Classic BSW projects. User workflow: multi-DBC drag-and-drop → network/ECU view → select target ECU (e.g., BDC) → inspect Rx/Tx messages and settings → resolve only missing/ambiguous policy → validate → export ARXML and/or generate C/H → optional existing SCons build. Users must not need to understand ARXML file organization or invoke Harmonize.

**Do not silently redefine the product** as a DBC viewer, an ARXML editor, a code generator only, or a UI mock. Those may be intermediate deliverables but are not the product.

## 1. Non-negotiable architecture gates (MUST)

| ID | Contract | Required evidence | Failure example |
|---|---|---|---|
| A01 | Electron + React/TypeScript is a UI shell, not the source of configuration truth. Existing Electron draft should be integrated rather than discarded without justification. | IPC call path from UI to engine; actual persisted data read from engine | UI state/JSON treated as canonical |
| A02 | C++20 core engine is independently executable and testable without Electron. | `CMakeLists.txt`, C++ library, CLI and CTest | All business logic implemented in Node/TypeScript |
| A03 | SQLite is the authoritative configuration store for engine-owned state; explicit transactions and schema migrations. | DB DDL, migrations, rollback tests | ARXML/JSON remains the only state, SQLite just cache |
| A04 | Two distinct dependency models: **reference graph** for existing model objects and **configuration rule dependency graph** for inferred objects, validation and generation. | Schema and tests for change → affected rules/artifacts | Only FK joins or full regenerate on every change |
| A05 | DBC source facts, project/user policy, derived ECUC values, and generated artifacts are distinct, provenance-tracked layers. | Tables/types and provenance tests | Guess E2E/SecOC/Diag from DBC and mark confirmed |
| A06 | ECUC **Definition (PDF)** and **Configuration Values** are distinct; `DEFINITION-REF` resolution and type/multiplicity/range checks. | Definition import, value mapping, invalid ref tests | One hard-coded SQL schema per AUTOSAR module |
| A07 | ARXML is a compatibility import/export format, not a mandatory intermediate in the new native path. Preserve unknown/vendor-specific extensions or explicitly block unsafe roundtrip. | Roundtrip semantic diff, unknown-extension tests | Unrecognized elements silently dropped |
| A08 | Existing Harmonize is **baseline/oracle for comparison**, not new engine implementation or required customer action. Existing Xtend may remain via adapter for not-yet-migrated modules. | Invocation trace and module ownership manifest | New pipeline shells out to Harmonize for every edit |
| A09 | Generator backend is swappable per module/artifact: C++ native and legacy Xtend adapter; no two backends may own same output path. | Generator interface, ownership manifest, conflict test | C++ and Xtend overwrite same `Com_Cfg.c` |
| A10 | Eclipse project compatibility: recognize and preserve `.project`, `.cproject`, `Build/`, `Configuration/`, `Generated/`, `Static_Code/` (or actual on-disk spelling), `Debug/`, `CryptoLib/`. Do not rewrite unrelated project metadata. | Project adapter + before/after manifest diff | Replace with a proprietary project folder |
| A11 | Existing Windows BAT and SCons compile/link path is supported through an optional build adapter; generation is independently invocable. | CLI `generate` without `build`; SCons integration test | Every edit forces full SCons build |
| A12 | Incremental processing is based on **semantic diff**, affected-rule sets, and output read dependencies. Unchanged files are not rewritten (`write-if-changed`). | change impact report + mtime/hash test | Every signal edit rewrites all module files |
| A13 | Validation is deterministic and explicit: AUTOSAR definition constraints vs vendor/project rules; errors have stable codes, object paths, and source locations. | Structured diagnostics, negative fixtures | Errors only as generic string or AI prose |
| A14 | UI network map is ECU-centric; link selection shows relevant messages. Deep 1:N ARXML references are not rendered as an unfiltered global graph. | UI tests/screenshots from fixture | Giant unreadable full reference graph |
| A15 | AI is optional explanation/suggestion layer, not authoritative validator or unreviewed mutator. | No-AI engine tests, suggestion approval flow | LLM answer directly edits authoritative DB |
| A16 | Do not silently modify source DBC, baseline project, static code, libraries, or baseline outputs. Mutations are explicit, atomic, scoped, and auditable. | readonly baseline test, rollback tests | Import overwrites source DBC |

**Architecture review result:** Any FAIL in A01–A16 means **ARCHITECTURE NOT ACCEPTED** regardless of UI polish or benchmark speed. `NOT_IMPLEMENTED` means **incomplete**, not failed-by-design; the report must show it clearly.

## 2. Reference boundaries / responsibility map

```text
Electron UI (existing draft)
  └── typed IPC bridge (input validation, no raw SQL)
      └── C++20 engine process/library + CLI
          ├── ProjectAdapter: Eclipse metadata, workspace inventory, safe paths
          ├── DbcImporter: source AST + semantic normalization + diagnostics
          ├── ArxmlAdapter: ECUC definitions/values + roundtrip/export
          ├── ConfigurationStore: SQLite, migrations, provenance, transactions
          ├── RuleEngine: declarative read/write sets, constraints, derivation
          ├── DependencyIndex: model refs + rule dependency + artifact reads
          ├── ValidationEngine: scoped and global constraints
          ├── GeneratorRegistry
          │   ├── Native C++ generator (first limited Com artifacts)
          │   └── Legacy Xtend adapter (remaining modules; baseline only for Harmonize)
          └── BuildAdapter: existing BAT/SCons (optional, isolated)
```

**Not a requirement:** rewrite all modules; rewrite RTE; replace SCons; eliminate Xtend immediately; optimize file-internal string patching; implement all vendor-specific ECUC semantics before inspecting real definitions.

## 3. DBC schema acceptance

### 3.1 Source entities (minimum target)

- `source_file`: canonical path/URI, content digest, import version, source ownership, diagnostics; **file identity is not merely basename**.
- `network/channel`: DBC logical network and project-specific physical bus mapping (do not conflate).
- `node`: ECU/BU_ declarations, stable identity within source scope.
- `message`: raw DBC ID, normalized CAN ID, standard/extended flag, name, DLC, transmitter(s) including extension semantics where supported.
- `signal`: start bit, bit length, Intel/Motorola byte order, signedness, factor, offset, min/max, unit, receivers, multiplex role/selector, initial/default where represented.
- `attribute_definition` / `attribute_assignment`: `BA_DEF_`, `BA_DEF_DEF_`, `BA_` scope and raw typed values; unknown attributes retained.
- `value_table` / `value_description`: `VAL_TABLE_`, `VAL_` semantics.
- `comment`: `CM_` with object scope.
- `multiplex_relation`: standard and extended multiplex constructs (`SG_MUL_VAL_`) when supported.
- `source_span`: file/line/column or equivalent provenance for diagnostics.

**Unsupported is not silently accepted:** if parser cannot represent a construct losslessly, emit structured `UNSUPPORTED_*` and prevent unsafe downstream generation, while optionally retaining raw source text. Explicitly distinguish a partial DBC parser PoC from a production parser.

### 3.2 DBC validation fixtures

| ID | Fixture | Expected check |
|---|---|---|
| D01 | Intel bit layout at DLC boundary | Correct physical bit occupancy |
| D02 | Motorola sawtooth numbering | Correct bit traversal; no linear-bit assumption |
| D03 | 11-bit vs 29-bit CAN ID, DBC extended flag | Correct normalization and range |
| D04 | Duplicate IDs within same channel | Conflict with source locations; project policy for duplicates |
| D05 | Same ID in different CAN channels | No false collision |
| D06 | Overlapping ordinary signals | Reject unless valid multiplex semantics |
| D07 | Mutually exclusive multiplexed signals overlap | Accept if disjoint selectors |
| D08 | Multi-DBC import with same message names/IDs | Deterministic conflict policy, no silent merge |
| D09 | Re-import unchanged file | Idempotent; no duplicate entities or generated changes |
| D10 | Modify only signal factor | Correct semantic diff and minimal affected artifacts |
| D11 | Unknown BA_/vendor attribute | Preserve or block unsafe generation; never silently drop |
| D12 | CAN FD / DLC semantics | Explicit version/policy; no assumption all payloads ≤8 bytes |
| D13 | Empty/invalid transmitter and receiver references | Diagnostics, no invented ECU mapping |
| D14 | Missing E2E/SecOC/Diag policy | `UNRESOLVED`, not guessed as validated |
| D15 | Invalid factor/offset, min>max, invalid bit length | Stable diagnostic codes |

**Implementation detail:** Standard DBC Motorola start bit is sawtooth-numbered. A naive `start_bit + length` validator is wrong. Signal overlap checking must account for multiplexing and actual bit placement.

## 4. ECUC / ARXML contract

1. Import and index ECUC Module Definition/PDF (`EcucParamConfContainerDef`, `EcucParameterDef`, `EcucReferenceDef`, multiplicity, types, ranges, defaults, variants).
2. Import Configuration Values separately and resolve every `DEFINITION-REF` to compatible definition and container scope.
3. Validate reference target type, parent membership, cardinality, conditional constraints, variants, project/vendor rules. FK existence is insufficient.
4. Preserve AUTOSAR paths, namespaces, relevant ordering, source file ownership, vendor extensions, version and variant metadata.
5. Export deterministic ARXML; compare **semantic canonical form**, not merely raw XML bytes. Do not normalize away meaningful order, variants, or extensions.
6. Treat RTE as legacy generator/validation integration initially. Rebuild only if relevant ASW interface/mapping dependencies actually change.

## 5. Incremental engine contract

- Semantic changes are keyed by stable object IDs and property paths; separate source updates from derived changes.
- Rules declare `reads`, `writes`, validation scope, and invalidation dependencies; include possible *creation* of objects not present in the reference graph.
- Apply changes transactionally; derive to fixed point or detect/report cycles; validate before publishing committed generated outputs.
- Compute affected artifact set from generator read sets; keep generator output ownership manifest.
- Generate deterministic content in staging; compare content hashes and atomically replace **only changed** outputs. Preserve unchanged file timestamps.
- If a fragment cache is implemented, it must invalidate on changed global ordering/index assignments; cache is optional until profiling justifies it.
- SQLite WAL allows concurrent readers but only one writer; do not design around concurrent SQLite write transactions. Thread pools can parallelize pure parsing, validation snapshots, independent generator tasks, and hashing where safe.
- **Invariant:** Incremental result must equal a clean full regeneration under the same model, rules, generator version and environment.

## 6. Eclipse / SCons / Windows compatibility

- Inventory `.project`, `.cproject`, toolchain settings, linked resources, environment, SConstruct/SConscript, `.bat`, output paths and optional external tools; inspect before modification.
- Keep original project read-only in baseline experiments. Clone to test workspace; preserve existing file encoding, line endings and path case where relevant.
- `Configuration/` is a likely ARXML/config location; `Generated/` output; `Static_Code/`, `CryptoLib/` should default to immutable. `Build/` and `Debug/` semantics must be discovered per project rather than assumed.
- Generator and build commands must be separately callable from CLI and Electron. Never require Eclipse GUI to execute CI tests.
- Do not run untrusted BAT/SCons automatically on file import; explicit user/build test action and sandboxed workspace.
- Validate generated include paths, compile definitions, linker inputs, compiler versions, ABI, and SCons dependency tracking.

## 7. Control group design: 3–4 existing projects

**No project names, vendor versions, MCU variants, or actual baseline metrics are to be invented.** Create a `baseline-manifest.yaml` per real project after inspection. Suggested distinct cases (select actual available projects):

| Baseline class | Selection criterion | Why |
|---|---|---|
| B1 | Small CAN-centric ECU with Com/PduR/CanIf | Basic functional equivalence |
| B2 | Multiple CAN channels / large DBC / multiplex | Cross-file/channel conflicts and scalability |
| B3 | Dcm + CanTp + security/E2E/SecOC policies as applicable | Vendor/project rule correctness |
| B4 | RTE/OS/MCAL/complex generated output and Eclipse/SCons | Legacy adapter and build compatibility |

For each project capture: source file hashes, DBC inputs, ECUC PDF/version, ECUC values, target ECU, selected configuration/variants, legacy Harmonize output, Xtend generator version, BAT/SCons commands, compiler/toolchain version, generated file manifest/hashes, build logs, and representative test evidence. **Never use a legacy output as the sole truth:** legacy may contain defects; discrepancies require triage against definition/spec/project rules.

### 7.1 Test arms and fairness

- **Control (C):** same input and target configuration through existing DBC import → manual/project policy → Harmonize → Xtend → SCons.
- **Experimental (E):** same input and target configuration through C++ importer/store/rules → native/legacy generator routing → same SCons toolchain.
- Record any manual choices and defaults explicitly so C and E receive equivalent intent. Keep source/toolchain versions and hardware fixed; distinguish cold start vs warm incremental changes.
- Compare baseline **full generation** separately from **single-property incremental**. Do not claim full-path speedup using only a native generator microbenchmark.

### 7.2 Verification matrix

| ID | Gate | Method | Pass condition |
|---|---|---|---|
| V01 | Source import | DBC parse and source fact comparison | All supported facts match; unsupported facts reported |
| V02 | ECU mapping | Tx/Rx per target ECU, multiple files/channels | Correct relation with no invented edges |
| V03 | ECUC structural validity | Definition-driven type/range/multiplicity/reference tests | No invalid config committed |
| V04 | ARXML semantic equivalence | Canonical semantic diff including variants/extensions | No unexplained differences |
| V05 | Generated code equivalence | Normalize only nonsemantic formatting, compare tables/constants/refs | No unexplained semantic differences |
| V06 | Build compatibility | Existing SCons/BAT compile/link on clone | Build succeeds under matched environment |
| V07 | Runtime equivalence | Available CANoe/CAPL, HIL/SIL or module tests | Required externally visible behavior unchanged |
| V08 | Incremental = full | Compare artifacts from incremental edit and clean full regeneration | Semantically identical outputs |
| V09 | Minimal invalidation | Trace rule/artifact invalidations for factor-only, ID-only, DLC, mapping edits | No unjustified regeneration; no missed dependent artifacts |
| V10 | No-op stability | Re-import same file, generate twice | Same hashes; unchanged files retain mtime |
| V11 | Failure atomicity | Inject parse, validation, generator, disk and build errors | No partial published config/output; clear error |
| V12 | Baseline isolation | Hash original project before/after all tests | Identical original source/project tree |
| V13 | Repeatability | Run same fixture repeatedly on clean workspace | Deterministic model and artifacts |
| V14 | UI/engine consistency | Reload UI and query engine for same ECU | Same messages, directions, status |
| V15 | AI isolation | Disable AI, feed wrong AI suggestion | Core still works; invalid changes rejected |
| V16 | Security of execution | Malicious paths, symlinks, malformed DBC, shell args | No workspace escape or command injection |

**Expected exceptions:** Differences that are verified as bug fixes or intentionally changed generator behavior must be documented, linked to a requirement and independently tested. A discrepancy is not automatically a failure of the new system or proof the old system is correct.

### 7.3 Golden fixtures and differential testing

Store sanitized/synthetic fixtures in repository; real corporate baselines only in approved private environment. Use an immutable baseline snapshot and per-test writable clones. Differential reports should include `source_id`, ECUC path, generator, output path, old/new semantic values, violated rule, and reproduction command. Compare artifact bytes for deterministic identical-version runs; compare semantic content across distinct generators. Maintain positive and negative fixtures and mutation-based cases.

## 8. Key metrics / 핵심 지표 (record measured values, never fabricate)

| Metric | Definition | Measurement |
|---|---|---|
| **Time to First ASW Build** | From accepted project/DBC inputs to first successful ASW build with required BSW config | End-to-end wall clock, manual intervention included |
| Full import latency | Multi-file DBC drop to validated persisted model | p50/p95 over repeated runs, cold/warm separate |
| ECU selection response | Select ECU → Rx/Tx map ready | p50/p95 UI and engine separately |
| Incremental edit latency | Commit one config property → validated affected artifacts ready | p50/p95 per change type |
| Full generation time | Identical model → complete generated C/H | CPU/wall time, cold/warm |
| Peak memory | Maximum process memory across UI and engine separately | Peak RSS/working set with tooling noted |
| Regenerated files | Files whose contents changed after one semantic edit | Count, bytes, reason graph |
| False invalidation rate | Unnecessary regeneration / regenerated artifacts | Compare to independently validated dependency expectation |
| Missed invalidation count | Required artifacts not regenerated | **Zero** in tested fixtures |
| Semantic discrepancy count | Unexplained config/code differences vs reference/spec | **Zero** for acceptance cases |
| Build pass rate | Successful builds / eligible matched-environment runs | Per baseline and test arm |
| No-op mutation count | Changed file hashes/mtimes on identical reimport | **Zero** |
| Validation coverage | Tested rule IDs / applicable implemented rule IDs | Report numerator and denominator; no unsupported claims |
| User intervention | Required manual choices/corrections per project | Record separately from time |

**No arbitrary performance targets are asserted here.** Baseline first, then set targets using real project measurements. Correctness gates are not traded for speed.

## 9. Agent behavior / Codex implementation contract

1. Before changing code, inspect repository tree, existing Electron UI, actual `.project`/`.cproject`, BAT/SCons, and tests. List what exists vs what is assumed.
2. Maintain `docs/architecture-deviations.md` for proposed deviations; do not silently substitute another language/database/build format.
3. Implement vertical slices with **real C++/SQLite behavior**, not mocks represented as complete functionality. For a stub, name it `NOT_IMPLEMENTED` in API and report.
4. Every change must include tests for behavior and a verification command. No fabricated test results.
5. Do not claim DBC full support when only `BO_`/`SG_` are parsed. Multiplex, attributes, comments, CAN FD, and extensions must be enumerated by actual coverage.
6. Preserve existing Electron UI and Eclipse project structure unless a specific change is justified and approved.
7. Do not silently write ARXML/DBC originals; prefer staged output and explicit export.
8. AI explanations must be grounded in engine-returned object IDs and diagnostics, not speculative code-only inference.
9. Always report: `Implemented`, `Not implemented`, `Known limitations`, `Tests executed + results`, `Affected files`, `Architecture gate status`.
10. If asked to audit rather than implement, **do not edit files**. Produce a matrix with paths, code symbols, test commands and observed results.

## 9.1 Historical conformance snapshot, before persisted DBC import (2026-11-08)

The newer implementation, executed checks and remaining gates are documented in [the current acceptance report](docs/verification/acceptance-2026-11-08.md). The snapshot and implementation order below describe the preceding slice and are retained as history, not current completion claims.

This is a repository-evidence snapshot, not an acceptance claim for the product. `PARTIAL` means some evidence exists but the full gate is not met. The current implemented vertical slice covers project-definition persistence, scoped DBC source/fact import, ECU Tx/Rx queries, and read-only Eclipse project inventory. It is not an AUTOSAR configuration or generation implementation.

| Gate | Status | Evidence / remaining gap |
|---|---|---|
| A01 | PARTIAL | Electron calls the C++ JSON-RPC engine; validated project definitions and explicitly imported DBCs persist in SQLite. Unsaved form edits and pre-import previews remain renderer state. |
| A02 | PASS | Standalone C++20 JSON-RPC executable and C++ unit-test target are defined; unit and protocol tests run without Electron. |
| A03 | PASS | SQLite v1→v2 migration, WAL, transactional project and DBC writes, restart restore, idempotent imports and injected rollback tests cover all currently implemented engine-owned state. ECUC values and generated artifacts are not implemented (A05–A09). |
| A04 | NOT_IMPLEMENTED | No model-reference graph or configuration-rule dependency graph. |
| A05 | PARTIAL | DBC source identity, original text, digest, parser version, diagnostics and parsed facts are persisted separately from the project definition. Derived ECUC values, artifact provenance and user overrides are absent. |
| A06 | NOT_IMPLEMENTED | No ECUC Definition/Configuration Values importer, `DEFINITION-REF` resolver or schema-driven validator. |
| A07 | NOT_IMPLEMENTED | No ARXML import/export or unknown-extension preservation/blocking path. |
| A08 | NOT_IMPLEMENTED | No legacy Xtend/Harmonize adapter or differential invocation path. The current engine does not invoke Harmonize. |
| A09 | NOT_IMPLEMENTED | No generator registry or artifact-ownership manifest. |
| A10 | PARTIAL | `project.inspect` reads and hashes an existing directory, recognizes `.project`/`.cproject`, refuses symlink/reparse traversal, and does not execute build scripts. It is not a project write adapter and is not qualified against real projects. |
| A11 | NOT_IMPLEMENTED | No optional SCons/BAT build adapter or separate generation/build commands. |
| A12 | PARTIAL | `dbc.diff` reports semantic message/signal changes. Rule read sets, impact analysis, output ownership and write-if-changed generation are absent. |
| A13 | PARTIAL | DBC and project draft checks return structured diagnostics; ECUC definition constraints, object-path-complete diagnostics and vendor/project rule separation are absent. |
| A14 | PARTIAL | Persisted `network.ecu` Tx/Rx queries power an ECU-focused UI list and peer/message filtering; unknown ECUs are rejected rather than fabricated. Automated renderer interaction fixtures/screenshots are absent. |
| A15 | PASS | No AI path is present in the authoritative engine/configuration flow; engine validation runs without AI. |
| A16 | PARTIAL | DBC import writes only SQLite; project inventory is read-only and records file hashes; database writes are transactional. Generator destinations, baseline immutability during publication and atomic generated-file writes are not implemented. |

| DBC fixtures | Status | Evidence / remaining gap |
|---|---|---|
| D01–D02 | PASS | Intel/Motorola bit occupancy, frame-boundary, byte crossing and mixed-endian overlap tests cover the supported layouts. |
| D03 | PASS | Standard/extended ID normalization, width and reserved-bit validation have parser fixtures. |
| D04–D06 | PASS | Duplicate frame/name conflicts are rejected within an explicitly assigned channel with source locations; separate channels are isolated; ordinary signal overlap is rejected. |
| D07 | PARTIAL | Simple multiplex selectors and mutually exclusive branches are checked; extended `SG_MUL_VAL_` and nested forms are not modeled. |
| D08–D09 | PASS | Multi-source imports reject same-channel frame/name conflicts, keep same IDs on separate channels independent, and make unchanged reimports idempotent. |
| D10 | PARTIAL | `dbc.diff` identifies semantic changes and imports replace a source's facts transactionally; rule/artifact impact and output invalidation are absent. |
| D11 | PASS | Unsupported declarations produce stable diagnostics and original DBC text is retained; generation remains blocked rather than claiming unsupported semantics are modeled. |
| D12 | PASS | Import requires an explicit Classic CAN/CAN FD choice; FD payload length and channel policy conflicts are validated. |
| D13 | PASS | Unknown senders/receivers are diagnosed and ECU queries reject undeclared target nodes instead of fabricating mappings. |
| D14 | PASS | Security/diagnostic policy remains explicitly unresolved and imports report generation blocked; no E2E/SecOC semantics are inferred from DBC. |
| D15 | PASS | Invalid scaling, non-finite values, reversed ranges, invalid bit lengths and stable parser issue codes have negative tests. |

| Verification | Status | Evidence / remaining gap |
|---|---|---|
| V01 | PARTIAL | Supported DBC facts are parsed, persisted, and queried with source identity and diagnostics; no independent source-fact oracle or full dialect comparison exists. |
| V02 | PASS | Persisted target-ECU Tx/Rx query has parser/storage and JSON-RPC restart coverage; unknown nodes are not synthesized. |
| V03–V09 | NOT_IMPLEMENTED | No ECUC/ARXML, generated C/H, baseline build, runtime, or legacy/native comparison harness. |
| V10 | PARTIAL | Stable source digest/revision and no-op reimport are tested; repeat generation is unavailable. |
| V11 | PARTIAL | Invalid import batches and injected SQLite write failures leave stored facts unchanged; filesystem publication and build failure atomicity are not applicable until those paths exist. |
| V12 | PARTIAL | Synthetic Eclipse inventory hashes files read-only and detects a changed baseline; no approved real-project baseline harness exists. |
| V13 | PARTIAL | SHA-256 vectors and deterministic inventory/import behavior are tested; generated-output determinism cannot yet be measured. |
| V14 | PARTIAL | Renderer startup restores persisted project and DBC source state through JSON-RPC; the Node integration test covers the APIs but automated renderer interaction tests are absent. |
| V15 | PASS | There is no AI-dependent engine behavior. |
| V16 | PARTIAL | JSON-RPC and project-root validation, read-only inventory, symlink/reparse refusal, and rollback tests exist; no generated-file path/publication threat tests exist. |

**Observed validation for this snapshot:**

- `node scripts/build-engine.mjs --output="$env:TEMP\studio-acceptance-build"` built the standalone engine and ran `studio-engine-tests.exe` and `studio-acceptance-tests.exe`; both passed, including v1 record-preserving migration, DBC import/idempotency/conflict/rollback, ECU query, and read-only inventory tests.
- With `STUDIO_ENGINE_PATH` set to that temporary executable, `npm.cmd run test:engine` passed, including `dbc.import`, `dbc.sources`, `network.ecu`, `project.inspect`, and a second engine process restoring project and DBC state.
- `npm.cmd run build` passed (TypeScript check and Vite production build).
- CMake/CTest were not run because CMake is not installed in the inspected environment.

**Next implementation order:**

1. Extend lossless DBC semantics for `VAL_`, `VAL_TABLE_`, comments, typed attributes and extended multiplexing, with source spans and round-trip/preservation tests (A05, D07, D11, V01).
2. Keep user policy and overrides distinct from imported DBC facts; add explicit provenance and audit history before introducing derived ECUC values (A03, A05, A13).
3. Import real, approved ECUC Definition and Configuration Value inputs and validate `DEFINITION-REF`, types, multiplicity and ranges; do not fabricate vendor defaults (A06–A07).
4. Add reference and rule-dependency indexes with change-to-affected-rule tests before implementing incremental generation (A04, A12).
5. Qualify an owned native artifact and optional BAT/SCons adapter against frozen, approved Eclipse project baselines, with write-if-changed, rollback and legacy comparison tests (A08–A11, A16, V03–V13).

### Ready-to-paste Codex audit prompt

```text
Read README.md, ARCHITECTURE_ACCEPTANCE.md and AGENTS.md. Audit the CURRENT repository against every A01–A16 architecture gate, D01–D15 DBC fixture and V01–V16 verification gate. Do not modify any files. For each gate provide PASS/FAIL/NOT_IMPLEMENTED/BLOCKED, evidence paths and symbols, the exact command run and observed result, and a minimal corrective action. Distinguish actual functionality from mocks, placeholders and UI-only demonstrations. Identify any violation of C++20 + SQLite source of truth, separation of model-reference vs rule-dependency graphs, preservation of Eclipse/SCons/BAT project compatibility, and baseline isolation. Never invent test outcomes. Conclude with the 5 highest-risk deviations, prioritized by correctness and irreversible data loss rather than by visual polish.
```

### Ready-to-paste Codex implementation prompt

```text
Read README.md and ARCHITECTURE_ACCEPTANCE.md first. Inspect the existing Electron project before writing code. Implement only the next smallest real vertical slice of the C++20/SQLite engine (for example: lossless-or-explicitly-unsupported DBC import of a documented subset, persisted in SQLite, queried through CLI and typed Electron IPC). Preserve existing Eclipse/SCons/BAT structure and avoid changing corporate baseline files. Write tests and execute them. Report architecture gate status and explicitly identify unsupported DBC syntax. Do not introduce a TypeScript-only engine, hard-code AUTOSAR configuration assumptions, or invoke Harmonize in the new engine path.
```

## 10. Completion report template

```markdown
# Architecture Conformance Report
- Commit / branch / environment:
- Repository inspected:
- Baselines used (B1–B4) and immutable source hashes:
- A01–A16 matrix (status, evidence, command, observed result):
- D01–D15 matrix (status, fixture, result):
- V01–V16 matrix (status, baseline, result):
- Supported DBC syntax and explicitly unsupported syntax:
- ECUC definition/value support and version/vendor limits:
- Generator ownership manifest and legacy/native split:
- Eclipse metadata and SCons/BAT compatibility:
- Performance results (cold/warm, p50/p95, memory, host):
- Deviations and risks:
- Unverified assumptions / missing real project inputs:
- Next minimal correctness-oriented engineering change:
```

## 11. Scope note

The precise AUTOSAR module definitions, vendor extensions, and baseline ECU-specific constraints cannot be truthfully finalized from discussion alone. They must be extracted from the 3–4 actual Eclipse projects and ECUC PDF/values under approved access. The **schema architecture and acceptance tests are specifications**, not evidence that the current implementation already satisfies them.
