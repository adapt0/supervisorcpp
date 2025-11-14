# supervisorcpp - Minimal supervisord Replacement in C++

A minimal, embedded-friendly replacement for supervisord written in C++23, designed for Alpine Linux environments.

## Project Status

**Phase 1: Configuration & Core Framework** ✅ COMPLETE

- [x] CMake build system
- [x] Configuration parsing (INI format)
- [x] Configuration data structures
- [x] Basic logging framework
- [x] Unit tests for configuration

**Phase 2: Process Management** ✅ COMPLETE

- [x] Process spawning (fork/exec)
- [x] User switching (setuid/setgid/initgroups)
- [x] Environment and working directory setup
- [x] Process state machine (STOPPED, STARTING, RUNNING, BACKOFF, FATAL, EXITED)
- [x] SIGCHLD handling with Boost.Asio
- [x] Event loop integration
- [x] Autorestart logic with retry backoff
- [x] Graceful shutdown (SIGTERM → SIGKILL)

**Phase 3: Log Capture & Rotation** ✅ COMPLETE

- [x] Async stdout/stderr capture via Boost.Asio pipes
- [x] Line-buffered log writing
- [x] Size-based log rotation on line boundaries
- [x] Automatic log directory creation
- [x] Configurable rotation size and backup count
- [x] Clean rotation (log → log.1 → log.2, etc.)

## Features

- **Lightweight**: Minimal binary size and memory footprint
- **Compatible**: Uses same configuration file format as supervisord
- **Modern C++**: Built with C++23 and Boost libraries
- **Embedded-friendly**: Designed for Alpine Linux with musl

## Requirements

- CMake 3.20+
- C++23 compatible compiler (GCC 11+, Clang 14+)
- Boost 1.75+ libraries:
  - Boost.Log
  - Boost.PropertyTree
  - Boost.Asio
  - Boost.Process
  - Boost.ProgramOptions
  - Boost.Test

## Building

```bash
# Create build directory
mkdir build
cd build

# Configure
cmake ..

# Build
make -j$(nproc)

# Run tests
ctest --verbose
# or
make test
```

## Usage

### supervisord (Phase 1 - Configuration Loading)

```bash
# Run with default config
./supervisord

# Run with custom config
./supervisord -c /path/to/config.ini

# Run in foreground
./supervisord -n

# Show help
./supervisord --help
```

### supervisorctl (Placeholder)

```bash
# Show help
./supervisorctl --help
```

## Configuration

Compatible with supervisord configuration format. See `SPECIFICATION.md` for detailed documentation.

Example minimal configuration:

```ini
[unix_http_server]
file=/run/supervisord.sock

[supervisord]
logfile=/var/log/supervisord.log
loglevel=info
user=root
childlogdir=/var/log/supervisor

[rpcinterface:supervisor]
supervisor.rpcinterface_factory = supervisor.rpcinterface:make_main_rpcinterface

[supervisorctl]
serverurl=unix:///run/supervisord.sock

[program:my_app]
command=/opt/apps/bin/my_app
autorestart=true
user=root
stdout_logfile=/var/log/supervisor/%(program_name)s.log
stdout_logfile_maxbytes=10MB
redirect_stderr=true
```

## Development Status

### Completed (Phases 1-3)
- ✅ Configuration parsing (INI format with includes)
- ✅ Configuration validation
- ✅ Logging framework (Boost.Log)
- ✅ Unit tests
- ✅ Basic command-line interface
- ✅ Process spawning and monitoring
- ✅ User switching (setuid/setgid/initgroups)
- ✅ SIGCHLD signal handling
- ✅ Process state machine (STOPPED, STARTING, RUNNING, BACKOFF, FATAL, EXITED)
- ✅ Autorestart logic with retry backoff
- ✅ Graceful shutdown (SIGTERM → SIGKILL)
- ✅ Boost.Asio event loop
- ✅ Async stdout/stderr capture with pipes
- ✅ Line-buffered log file writing
- ✅ Size-based log rotation on line boundaries

### Upcoming Phases

**Phase 4**: RPC Interface
- Unix socket server
- XML-RPC protocol
- Remote control methods

**Phase 5**: supervisorctl Client
- Interactive CLI
- Command implementation
- Output formatting

**Phase 6**: Production Polish
- Edge case handling
- Integration testing
- Documentation

## Testing

Unit tests are located in `tests/`. Run with:

```bash
cd build
ctest --verbose
```

Individual test executables:
- `config_test` - Configuration parsing tests

## License

See LICENSE file for details.

## Contributing

This is a minimal replacement implementation. See `SPECIFICATION.md` for design goals and limitations.
