# Architecture acceptance evidence — 2026-11-08

Overall result: **INCOMPLETE**. This report covers the next four implementation slices specified in the historical acceptance snapshot. It does not claim the complete AUTOSAR product or legacy parity is implemented. `NOT_IMPLEMENTED` below includes gates with partial infrastructure; that evidence is described explicitly. `BLOCKED` identifies qualification that needs real external inputs.

## Implemented

- Transactional SQLite v1→v2 migration; relational DBC source/node/message/signal/receiver facts, original supplied text, SHA-256, parser version, revisions, source ownership and lines. Exact replay is idempotent. Whole-batch validation/conflict/write failures preserve committed facts.
- Explicit channel and classic CAN/CAN FD scope, duplicate-ID rejection within one scope, deterministic source listing, stable source identities across restart, and database-backed ECU Tx/Rx queries.
- CAN ID/layout/scaling/range validation, simple multiplex exclusivity, comma-separated receivers and explicit unsupported/unresolved diagnostics. Placeholder and undeclared ECUs never become resolved graph peers.
- Read-only Eclipse file/folder inventory with SHA-256, missing-metadata diagnostics, reparse-point protection and no script execution. UI source saving, ECU/peer selection, restart source restore and inventory action.

## Executed verification

Host: Windows, MinGW GCC/G++ 13.2.0, Node.js 24.13.0, vendored SQLite. No real B1–B4 projects or vendor toolchains were supplied. The synthetic Eclipse fixture is smoke input only.

| Command | Observed result |
|---|---|
| `node scripts/build-engine.mjs --output="$env:TEMP\studio-acceptance-build"` | Standalone engine and both C++ test executables built; unit and acceptance suites passed, including record-preserving v1 migration and same-channel duplicate-name conflict. |
| `npm.cmd run test:engine` with `STUDIO_ENGINE_PATH` pointing to the acceptance build | JSON-RPC smoke suite passed for project persistence/restart, DBC import/source listing, ECU query, and project inventory. |
| `npm.cmd run test:acceptance` with the same engine | Multi-source import, no-op replay, canonical identity, conflicts, invalid-request recovery, restart, ECU queries, SHA-256 against Node's independent crypto implementation, source hash/mtime preservation and project inventory passed. |
| `npm.cmd run build` | TypeScript and Vite production build passed. |
| CMake/CTest | BLOCKED: CMake/CTest are not installed. Both equivalent C++ executables were compiled and run directly. |

C++ coverage includes SHA-256 empty/abc/million-byte known vectors, streaming parity, actual v1-schema migration, injected SQLite fact-write rollback, typed relationships after restart, classic/extended identifiers, FD policy, Intel/Motorola overlap/boundaries, legal/illegal simple multiplex overlap, invalid scaling/ranges, unknown ECUs and read-only manifest mutation detection. Protocol coverage additionally checks directory junction/symlink refusal without following the target. UI interaction itself has not been automated.

## A01–A16

| Gate | Status | Evidence and remaining work |
|---|---|---|
| A01 | PARTIAL | Renderer calls typed preload/JSON-RPC and rehydrates committed project/DBC facts from the engine; automated renderer persistence/restore interaction coverage is absent. |
| A02 | PASS | `studio-core`, standalone `studio-engine` and two C++ test executables; direct MinGW build and both suites executed without Electron. CTest targets are defined but not executed here. |
| A03 | PASS | `project_store.cpp`: WAL, FK enforcement, migrations, writer transactions and rollback/restart tests for implemented engine-owned state. Future ECUC/artifact persistence is absent. |
| A04 | NOT_IMPLEMENTED | Relational source relationships are not a model-reference index or rule-dependency graph. |
| A05 | PARTIAL | Source facts/raw provenance are stored; complete policy provenance, derived ECUC and artifact layers are absent. |
| A06 | NOT_IMPLEMENTED | No dynamic ECUC definition/value importer or definition-driven validation. |
| A07 | NOT_IMPLEMENTED | No ARXML importer/exporter or extension-aware semantic roundtrip. |
| A08 | NOT_IMPLEMENTED | No legacy oracle adapter/ownership manifest. The current native path never invokes Harmonize. |
| A09 | NOT_IMPLEMENTED | No generator registry/backend routing/output-ownership conflict checks. Generation is blocked. |
| A10 | PARTIAL | Read-only inventory recognizes metadata and hashes ordinary project files; full Eclipse metadata/resource/toolchain interpretation remains absent. |
| A11 | NOT_IMPLEMENTED | No optional BAT/SCons build adapter or qualified generation/build path. Inspection never runs scripts. |
| A12 | PARTIAL | Stateless semantic diff and idempotent imports exist; affected-rule/artifact sets and write-if-changed generation do not. |
| A13 | PARTIAL | Stable DBC/import codes, lines and conflict paths exist; complete object-path diagnostics and definition/vendor rule separation are absent. |
| A14 | PARTIAL | ECU/channel/peer selectors and focused Tx/Rx messages are connected to engine queries; automated UI fixture/screenshots and complete UX qualification remain missing. |
| A15 | PASS | All engine and protocol tests operate without AI; no AI mutation/validation path exists. |
| A16 | PARTIAL | Source mtime/hash preservation, read-only inventory and SQLite rollback have executed evidence; generated-file staging/publication ownership and real baseline qualification remain absent. |

## D01–D15

| Gate | Status | Evidence / limits |
|---|---|---|
| D01–D02 | PASS | Independent expected Intel and Motorola sawtooth vectors, payload boundaries and mixed-endian occupied-bit overlap. |
| D03 | PASS | 11/29-bit normalization and reserved-bit rejection tested; JSON exposes raw frame ID, normalized CAN ID and IDE flag. |
| D04–D05 | PASS | Same-channel collision rejects all source writes; identical IDs on different explicit channels remain distinct. |
| D06–D07 | PASS | Simultaneous overlap is rejected; disjoint simple multiplex selectors permit overlap. Extended/nested multiplexing remains explicitly unsupported. |
| D08 | PASS | Canonical source URI, not basename; two same-named inputs remain distinct and same-channel conflicts are never merged. |
| D09 | PASS | Source replay reports `changed=false`, stable source identity and unchanged revision; restart restores relationships. Artifact stability is not claimed. |
| D10 | PARTIAL | Factor changes update source revision; stateless diff exists, but minimal affected artifacts cannot be tested without generators. |
| D11 | PASS | Unknown record text remains in the stored original; unsupported diagnostics persist and generation is blocked. Unknown attribute semantics are not interpreted. |
| D12 | PASS | Explicit classic/FD policy and permitted FD byte lengths tested; unknown vendor frame attributes remain unsupported. |
| D13 | PASS | Malformed/unknown/placeholder references are diagnosed; unknown ECU queries fail and unknown transmitters do not become resolved peers. |
| D14 | PASS | Returned policy status is `UNRESOLVED`; no E2E/SecOC/diagnostic defaults are guessed. |
| D15 | PASS | Nonfinite/zero factors, nonfinite offsets, reversed bounds and invalid bit lengths are rejected with stable codes. |

PASS here is limited to the documented supported DBC subset. It is not full-dialect acceptance.

## V01–V16 and external inputs

| Gate | Status | Evidence / remaining input |
|---|---|---|
| V01 | PARTIAL | Supported parser/store facts and raw retention tested; independent full-dialect inventory comparison absent. |
| V02 | PASS | Explicit source-scoped node references, channel isolation and Tx/Rx protocol queries tested; no invented mapping. |
| V03–V07 | BLOCKED | Need actual vendor ECUC definitions/values, approved legacy outputs/tool versions, matched compiler/SCons environment and runtime test evidence. Relevant adapters/generators are also unimplemented. |
| V08–V09 | NOT_IMPLEMENTED | No artifact generator, rule-read sets or incremental/full equivalence path. |
| V10 | PARTIAL | No-op import tested; no generated-file hashes/timestamps exist to qualify this full gate. |
| V11 | PARTIAL | Project and DBC fact-write rollback tested; artifact/filesystem/build failure injection remains absent. |
| V12 | PARTIAL | Synthetic fixture read-only inventories and source preservation tested; no real immutable B1–B4 baseline harness. |
| V13 | PARTIAL | Deterministic parser replay, persisted source replay and inventory repeatability are tested; repeatable generated artifacts do not exist yet. |
| V14 | PARTIAL | Stored sources and ECU facts survive restart and UI reads them; automated renderer interaction coverage absent. |
| V15 | PASS | All tests execute without AI. |
| V16 | PARTIAL | Invalid paths/requests and symlink/reparse refusal tested for read-only inventory; future build/export hostile-path cases remain absent. |

Next work: formal reference/rule dependency indexes, complete source/policy provenance and persisted semantic change sets; extend parser coverage from actual DBC corpora. Obtain real ECUC and 3–4 approved Eclipse/SCons projects before qualifying ARXML/code generation or claiming legacy equivalence. No speedup/runtime/ABI measurements are invented.

Affected code: CMake/core build definition; `engine/src/project/{project_store,digest,project_inventory}.cpp`; DBC parser; JSON-RPC dispatch; Electron main/preload, renderer and types; C++/protocol test suites; build helper. See `docs/architecture-deviations.md` for boundaries.
