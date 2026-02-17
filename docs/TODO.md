# TODO

Items grouped by priority. Spec references are to [SPECIFICATION.md](SPECIFICATION.md).

## P1 — Spec-required

*(all P1 items completed)*

## P2 — Production readiness (spec Phase 6)

*(all P2 items completed)*

## P3 — Future / out of scope

These are beyond the spec's scope limitations (§1.3) or not needed for our target environment.

- [ ] **PR_SET_NO_NEW_PRIVS** — set on child processes after privilege drop. Neither Python supervisord nor most process managers do this; could break setuid binaries and file capabilities in child processes
- [ ] **autorestart tri-state** — Real supervisord accepts `false`/`unexpected`/`true` (default: `unexpected`). Currently a `bool`. Spec §1.3 explicitly scopes this out: *"Simple autorestart behavior (no 'unexpected' mode)"*
- [ ] **exitcodes** — List of expected exit codes. Only meaningful with `autorestart=unexpected`
- [ ] **priority** — Process start ordering. Spec §1.3: *"Individual process control (no process groups)"*
- [ ] **unix_http_server auth** — `username`/`password` for socket authentication. Spec §5.2: explicitly unsupported
- [ ] **Daemon mode** — Not needed; runs under OpenRC on Alpine (or systemd/Docker elsewhere). Init system handles backgrounding. `nodaemon` config option parsed but intentionally ignored

## Done

### Implementation phases
- [x] Phase 1: Configuration & Core Framework
- [x] Phase 2: Process Management
- [x] Phase 3: Log Capture & Rotation
- [x] Phase 4: RPC Interface
- [x] Phase 5: supervisorctl Client
- [x] Phase 6: Production Polish & Testing

### Recent
- [x] RPC fault propagation — server handlers throw descriptive errors; client parses XML-RPC faults structurally
- [x] `getState()` tracks daemon state via `std::atomic<DaemonState>`
- [x] `Supervisord` class — encapsulates daemon state, process manager, RPC server
- [x] Spirit::Qi environment parser — handles commas in quotes, escaped quotes, single quotes
- [x] `pt_get` rewrite with `if constexpr` — strict validation for numeric and boolean values
- [x] `parse_string` calls `validate_config_()` — consistent with `parse_file`
- [x] Robustness tests migrated to `parse_string`
- [x] `BOOST_CHECK_EXCEPTION` with `msg_contains` for meaningful error assertions
- [x] Inline comment stripping via `CommentStrippingBuf`
- [x] Socket permissions — `set_socket_permissions()` (chmod 0600)
- [x] Pidfile — RAII `PidFileGuard`, config + CLI `--pidfile` override, path validation
- [x] Exponential backoff — `min(2^retry, 60)` delay in BACKOFF state, replaces fixed 1-second retry
- [x] stderr_logfile — Independent stderr capture with async pipe reads, separate LogWriter, rotation
- [x] umask — `[supervisord] umask` (default `022`) at daemon startup; per-process `[program:x] umask` before `execve()`
