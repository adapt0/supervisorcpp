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
supervisor-lib/           # Static library (supervisord_lib)
  config/                 # INI config parsing, types, validation
  process/                # Process lifecycle, manager, log writer, child setup
  rpc/                    # XML-RPC server, socket utilities
  util/                   # Logger, error types, path utilities

supervisor-app/           # Busybox-style multi-call binary
  main.cpp                # Dispatches by argv[0]: supervisord (default) or supervisorctl
  supervisord.cpp         # Daemon entry point
  supervisorctl.cpp       # CLI client entry point

tests/
  config/                 # Config parser unit + robustness tests
  rpc/                    # XML-RPC, dispatch, connection tests
  util/                   # String + security utility tests
  logger/                 # LogWriter tests
  integration/            # Full lifecycle integration tests
  data/                   # Test .ini config files
```

## Testing

- **Quality over quantity** — prefer consolidated, easy-to-maintain tests over many boilerplate-heavy ones. A single well-structured test that writes, reads back, checks size, and flushes is better than four separate tests with duplicated setup.
- **Boost.Test** framework with `BOOST_AUTO_TEST_CASE` / `BOOST_FIXTURE_TEST_CASE`
- New tests: one `add_unit_test()` call in CMakeLists.txt (single source file per executable)
- Integration tests start a real supervisord instance — they need test configs in `tests/data/`

## Key Conventions

- **C++23** standard, compiled with `-Wall -Wextra -Wpedantic`
- **Headers alongside implementation** — `.h` and `.cpp` live in the same directory
- **Header-only utilities** for small modules: `util/errors.h`, `util/path.h`, `config/validation.h`, `process/setup.h`, `rpc/socket_util.h`
- **Boost.Asio** for all async I/O — single-threaded event loop, no threads for I/O
- **Boost.PropertyTree** for INI config parsing and XML-RPC parsing
- **Security checks** are distributed across components, not centralized (see `SECURITY_AUDIT.md`)
- Daemon runs as **root** — security-sensitive code throughout

## Dependencies

- CMake 3.20+, C++23 compiler (GCC 13+ / Clang 16+)
- Boost 1.75+ (log, filesystem, system, thread, program_options, asio, property_tree, unit_test_framework)

## Other Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Architecture decisions, design patterns, extension guides
- [SECURITY_AUDIT.md](SECURITY_AUDIT.md) — Threat model and security hardening details
- [SPECIFICATION.md](SPECIFICATION.md) — Original project specification
- [README.md](README.md) — User-facing documentation
