# supervisorcpp

Minimal supervisord replacement in C++23 for Alpine Linux (musl libc). Single Boost dependency.

## Build & Test

```bash
cd build && cmake .. -G Ninja && ninja -j$(nproc)
ctest --output-on-failure
```

Prefer **Ninja** over Make — it's faster and already available in the build environment.

## Code Organization

```
supervisor-lib/
  src/                    # Static library (supervisor_lib)
    config/               # INI config parsing, types, validation
    process/              # Process lifecycle, manager, log writer, child setup
    rpc/                  # XML-RPC server, socket utilities
    util/                 # Logger, error types, path utilities
  tests/                  # Library tests
    config/               # Config parser unit + robustness tests
    rpc/                  # XML-RPC, dispatch, connection tests
    util/                 # String + security utility tests
    logger/               # LogWriter tests
    integration/          # Full lifecycle integration tests
    data/                 # Test .ini config files

supervisor-app/
  src/
    main.cpp              # Dispatches by argv[0]: supervisord (default) or supervisorctl
    args_parser.cpp/.h    # Shared CLI argument parsing
    daemon/               # Daemon (supervisord)
      supervisord.cpp/.h  # Daemon entry point + class
      daemon_state.h      # Daemon state enum
      rpc_handlers.h      # Shared process RPC handler registration
    ctl/                  # Controller client (supervisorctl)
      supervisorctl.cpp   # CLI client entry point
      supervisorctl.h     # SupervisorCtlClient class + XmlRpcError
  tests/                  # App-level tests
    ctl_handler_test.cpp  # Supervisorctl interaction tests over real socket
```

## Testing

- **Quality over quantity** — prefer consolidated, easy-to-maintain tests over many boilerplate-heavy ones. A single well-structured test that writes, reads back, checks size, and flushes is better than four separate tests with duplicated setup.
- **Boost.Test** framework with `BOOST_AUTO_TEST_CASE` / `BOOST_FIXTURE_TEST_CASE`
- Lib tests: `add_lib_test()` in CMakeLists.txt (single source file per executable)
- App tests: `add_app_test()` — also gets app include path for `supervisorctl.h` etc.
- Integration tests start a real supervisord instance — they need test configs in `supervisor-lib/tests/data/`

## Key Conventions

- **Const correctness** — prefer `const` by default for local variables, references, and pointers. Use `const auto` for values that don't need mutation.
- **C++23** standard, compiled with `-Wall -Wextra -Wpedantic`
- **Headers alongside implementation** — `.h` and `.cpp` live in the same directory
- **Header-only utilities** for small modules: `util/errors.h`, `util/path.h`, `config/validation.h`, `process/setup.h`, `rpc/socket_util.h`
- **Boost.Asio** for all async I/O — single-threaded event loop, no threads for I/O
- **Boost.PropertyTree** for INI config parsing and XML-RPC parsing
- **Security checks** are distributed across components, not centralized (see `docs/SPECIFICATION.md` §6)
- Daemon runs as **root** — security-sensitive code throughout

## Dependencies

- CMake 3.20+, C++23 compiler (GCC 13+ / Clang 16+)
- Boost 1.75+ (log, filesystem, system, thread, program_options, asio, property_tree, unit_test_framework)

## Other Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Architecture decisions, design patterns, extension guides
- [docs/SPECIFICATION.md](docs/SPECIFICATION.md) — Project specification, threat model
- [docs/TODO.md](docs/TODO.md) — Known issues and future work
- [README.md](README.md) — User-facing documentation
