# Architecture implementation boundaries

No alternative language, database or project/build format replaces the specified architecture. Electron remains a shell, `studio-core` is C++20, and SQLite owns committed project and supported DBC facts.

- SQLite schema v2 implements the source-fact layer and explicit channel/frame policy only. Complete project-policy provenance, derived ECUC values, model references, rule dependencies and artifact ownership remain unimplemented.
- DBC input is the UTF-8 text supplied by the client, retained without discarding unsupported records. This is not a serializer or byte-exact roundtrip guarantee for arbitrary source encodings. ASCII path case aliases are normalized on Windows; complete Unicode case alias handling is not implemented.
- Message and signal identities use source identity plus their names. Rename reconciliation and persisted semantic change sets are not implemented. `dbc.diff` remains a stateless comparison; no incremental-generation claim follows from it.
- All generation is explicitly blocked. The implementation does not infer E2E, SecOC or diagnostic policy, invent vendor definitions, invoke Harmonize, or write generated C/H/ARXML.
- Project inventory hashes files and preserves folder spelling. It does not resolve Eclipse linked-resource/toolchain semantics, establish SCons build commands, or qualify a real legacy baseline. Reparse points are refused or diagnosed without traversal.
- The UI has build/type verification and engine protocol coverage. Automated Electron interaction, visual checks, large-corpus performance and real ECU runtime equivalence remain unverified.
- The MinGW/Node build script is an additional host-tooling fallback. CMake remains the primary build definition. Tests use isolated temporary databases/workspaces and do not run fixture BAT/SCons scripts.
