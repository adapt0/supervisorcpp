# Architecture

## Design Decisions

### Busybox-Style Multi-Call Binary

Single `supervisor` binary dispatches by `argv[0]`. Symlinks (`supervisord`, `supervisorctl`) select the mode. Default mode is daemon. The `supervisor ctl` subcommand also works.

This matches Alpine Linux philosophy and halves the installed size versus two separate binaries.

### Async I/O with Boost.Asio

All I/O is event-driven through a single `boost::asio::io_context` run loop:

```cpp
boost::asio::io_context io_context;
ProcessManager pm(io_context);
RpcServer rpc(io_context, socket_path, pm);
io_context.run();
```

This covers Unix domain sockets (RPC), signal handling (SIGCHLD, SIGTERM), and async pipe reads (log capture). Single-threaded — no mutexes needed.

### Header-Only Utilities

Small modules are header-only to avoid linking complexity: error types, path helpers, validation functions, process setup, socket utilities. Larger components (config parser, process manager, RPC server, logger) have separate `.cpp` files.

### Security Distribution

Security is not centralized. Each component owns its security concerns:

| Component | Security Responsibility |
|---|---|
| `config/validation.h` | Config file ownership, path traversal, env sanitization |
| `process/setup.h` | Privilege drop, resource limits, FD isolation |
| `rpc/socket_util.h` | Socket permissions (0600) |
| `util/path.h` | Path canonicalization |

Full details in [SECURITY_AUDIT.md](../SECURITY_AUDIT.md).

## Process State Machine

```
STOPPED (0) ─start→ STARTING (10) ─success→ RUNNING (20)
     ↑                   ↓ fail              ↓ exit
     │              BACKOFF (30)          EXITED (100)
     │                   ↓                    ↓
     │              FATAL (200)          STOPPING (40)
     └─────────────────────────────────────────┘
```

States and transitions are defined in `supervisor-lib/process/process.h`.

## XML-RPC Methods

Implemented in `supervisor-lib/rpc/rpc_server.cpp`, registered in `register_handlers()`:

- `supervisor.getState` — daemon state
- `supervisor.getAllProcessInfo` / `supervisor.getProcessInfo` — process info
- `supervisor.startProcess` / `supervisor.stopProcess` / `supervisor.restart`
- `supervisor.startAllProcesses` / `supervisor.stopAllProcesses`
- `supervisor.shutdown`

## Signal Handling

- **SIGCHLD**: Reap child processes, update state machine
- **SIGTERM/SIGINT**: Graceful shutdown (stop all children, then exit)
- **SIGPIPE**: Ignored
- Per-process configurable stop signal (TERM, HUP, INT, etc.)

## Log Capture

Stdout/stderr from child processes are captured via async pipes using `boost::asio::posix::stream_descriptor`. Log rotation is automatic based on `maxbytes` config, with configurable backup count. Implementation in `supervisor-lib/process/log_writer.cpp`.

## Extension Guides

### Adding a New RPC Method

1. Declare in `supervisor-lib/rpc/rpc_server.h`:
   ```cpp
   std::string handle_my_method(const std::vector<std::string>& params);
   ```

2. Implement in `supervisor-lib/rpc/rpc_server.cpp`.

3. Register in `register_handlers()`:
   ```cpp
   handlers_["supervisor.myMethod"] = [this](auto& p) { return handle_my_method(p); };
   ```

### Adding a New supervisorctl Command

1. Add handler in `supervisor-app/supervisorctl.cpp`:
   ```cpp
   void handle_mycommand(SupervisorctlClient& client, const std::vector<std::string>& args);
   ```

2. Wire it in `execute_command()`:
   ```cpp
   if (cmd == "mycommand") { handle_mycommand(client, args); return 0; }
   ```

### Adding a New Busybox Mode

1. Create `supervisor-app/mymode.cpp` with `int mymode_main(int argc, char* argv[])`.
2. Add dispatch in `supervisor-app/main.cpp`.
3. Add source to `CMakeLists.txt` executable list.
4. Add symlink in `CMakeLists.txt` post-build step.
