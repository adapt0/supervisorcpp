# supervisorcpp - Minimal Supervisord Replacement in C++

A lightweight, high-performance process supervisor written in C++23 for embedded Linux environments (Alpine Linux with musl). Compatible with supervisord configuration files, command-line tools, and RPC interface.

## Project Status

**Phase 1: Configuration & Core Framework** ✅ COMPLETE
**Phase 2: Process Management** ✅ COMPLETE
**Phase 3: Log Capture & Rotation** ✅ COMPLETE
**Phase 4: RPC Interface** ✅ COMPLETE
**Phase 5: supervisorctl Client** ✅ COMPLETE
**Phase 6: Production Polish & Testing** ✅ COMPLETE

## Features

✅ **Process Management**
- Start, stop, restart processes
- Automatic restart with configurable retry backoff
- Process state tracking (STOPPED, STARTING, RUNNING, BACKOFF, STOPPING, EXITED, FATAL)
- Multiple processes managed simultaneously
- Signal handling (TERM, HUP, INT, QUIT, KILL, USR1, USR2)

✅ **Configuration**
- Compatible with supervisord INI format
- Include file support with glob patterns
- Variable substitution `%(program_name)s`
- Environment variables with quote stripping
- Working directory support
- Comprehensive validation with clear error messages

✅ **Logging**
- Stdout/stderr capture with async I/O
- Size-based log rotation on line boundaries
- Configurable log levels (DEBUG, INFO, WARN, ERROR)
- Individual log files per process
- Automatic log directory creation

✅ **RPC Interface**
- XML-RPC over Unix domain sockets
- Compatible with supervisorctl
- Full API: getState, getAllProcessInfo, startProcess, stopProcess, restart, shutdown

✅ **supervisorctl Client**
- Interactive and non-interactive modes
- Commands: status, start, stop, restart, shutdown, reload, help
- Formatted output matching supervisord style

✅ **Robustness & Testing**
- 64 comprehensive test cases (100% passing)
- Config parser robustness tests (23 tests)
- XML-RPC parser validation (24 tests)
- Integration tests covering full workflows (7 tests)
- Unit tests (10 tests)

## Building

### Requirements

- C++23 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- Boost 1.75+ (log, filesystem, system, asio, property_tree, program_options, unit_test_framework)

### Build Instructions

```bash
# Install dependencies (Alpine Linux)
apk add cmake g++ boost-dev

# Or Ubuntu/Debian
apt-get install cmake g++ libboost-all-dev

# Build
mkdir build
cd build
cmake ..
make -j$(nproc)

# Run tests (64 tests, ~0.15 seconds)
ctest --output-on-failure

# Install
sudo make install
```

### Binary Architecture

supervisorcpp uses a **busybox-style multi-call binary** approach:

- **Single binary**: `supervisor` (2.8 MB)
- **Symlinks**: `supervisord` → `supervisor`, `supervisorctl` → `supervisor`
- **Mode selection**: Based on `argv[0]` (symlink name) or explicit `supervisor ctl` command

This design:
- ✅ Reduces installed footprint (~50% smaller than separate binaries)
- ✅ Follows Alpine Linux philosophy (matches busybox pattern)
- ✅ Simplifies distribution and installation
- ✅ Easy to extend with new modes (e.g., `supervisorstat`)

**Usage modes**:
```bash
# As daemon (default)
supervisor -c /etc/supervisord.conf
./supervisord -c /etc/supervisord.conf  # Via symlink

# As controller
supervisor ctl status                    # Explicit mode
./supervisorctl status                   # Via symlink
```

### Directory Structure

```
supervisor-lib/              # Library code (headers + implementation)
  config/                    # Configuration parsing & validation
  process/                   # Process management & setup
  rpc/                       # XML-RPC server & utilities
  util/                      # Common utilities & errors

supervisor-app/              # Application entry points
  main.cpp                   # Unified entry point (argv[0] dispatch)
  supervisord.cpp            # Daemon mode implementation
  supervisorctl.cpp          # Controller mode implementation

tests/                       # Test suites (64 tests)
```

## Configuration

### Basic Configuration File

```ini
[unix_http_server]
file=/var/run/supervisord.sock

[supervisord]
logfile=/var/log/supervisord.log
loglevel=info

[supervisorctl]
serverurl=unix:///var/run/supervisord.sock

[program:myapp]
command=/usr/local/bin/myapp --config /etc/myapp.conf
directory=/var/lib/myapp
user=appuser
autorestart=true
stdout_logfile=/var/log/myapp.log
stdout_logfile_maxbytes=50MB
startsecs=5
startretries=3
stopwaitsecs=10
stopsignal=TERM
environment=PATH="/usr/local/bin:/usr/bin",CONFIG_ENV="production"
```

### Configuration Options

#### [unix_http_server] Section
- `file` - Path to Unix domain socket for RPC (default: `/run/supervisord.sock`)

#### [supervisord] Section
- `logfile` - Supervisord main log file (default: `/var/log/supervisord.log`)
- `loglevel` - Log level: debug, info, warn, error (default: `info`)
- `user` - Run as user (default: `root`)
- `childlogdir` - Directory for AUTO child logs (default: `/var/log/supervisor`)

#### [supervisorctl] Section
- `serverurl` - URL to supervisord RPC interface (default: `unix:///run/supervisord.sock`)

#### [program:x] Section (x = program name)
Required:
- `command` - Command to run (supports variable substitution)

Optional:
- `directory` - Working directory for process
- `user` - Run process as this user (default: `root`)
- `autorestart` - Auto-restart: true/false (default: `true`)
- `startsecs` - Seconds process must stay up to be considered successful (default: `1`)
- `startretries` - Number of restart attempts before giving up (default: `3`)
- `stopwaitsecs` - Seconds to wait before SIGKILL after SIGTERM (default: `10`)
- `stopsignal` - Signal to send on stop: TERM, HUP, INT, QUIT, KILL, USR1, USR2 (default: `TERM`)
- `stdout_logfile` - Path to stdout log (supports `%(program_name)s`)
- `stdout_logfile_maxbytes` - Max size before rotation: 1KB, 10MB, 1GB (default: `50MB`)
- `stdout_logfile_backups` - Number of backup files to keep (default: `10`)
- `redirect_stderr` - Redirect stderr to stdout (default: `false`)
- `environment` - Environment variables: `KEY1="value1",KEY2="value2"`

### Variable Substitution

Currently supported:
- `%(program_name)s` - Replaced with program name

Example:
```ini
[program:webapp]
command=/usr/bin/webapp
stdout_logfile=/var/log/%(program_name)s.log
# Expands to: /var/log/webapp.log
```

## Usage

### Running supervisord

```bash
# Start supervisord
supervisord -c /etc/supervisord.conf

# Start in foreground (no daemon)
supervisord -c /etc/supervisord.conf -n

# Specify config file
supervisord --configuration /path/to/config.ini
```

### Using supervisorctl

#### Non-interactive Mode
```bash
# Show all process status
supervisorctl status

# Start a process
supervisorctl start myapp

# Stop a process
supervisorctl stop myapp

# Restart a process
supervisorctl restart myapp

# Shutdown supervisord
supervisorctl shutdown
```

#### Interactive Mode
```bash
$ supervisorctl
supervisord> status
myapp        RUNNING    pid 1234, uptime 0:05:23
webapp       STOPPED

supervisord> start webapp
webapp: started

supervisord> restart myapp
myapp: stopped
myapp: started

supervisord> help
status          Show process status
start <name>    Start a process
stop <name>     Stop a process
restart <name>  Restart a process
shutdown        Shutdown supervisord
reload          Reload configuration
help            Show this help
exit            Exit supervisorctl

supervisord> exit
```

## Testing

### Test Suites

```bash
# Run all tests
cd build
ctest --output-on-failure

# Run specific test suites
./config_test                # Configuration parser tests (10 tests)
./parser_robustness_test     # Parser robustness (23 tests)
./xmlrpc_parser_test         # XML-RPC parser (24 tests)
./integration_test           # Integration tests (7 tests)
```

### Test Coverage Summary

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| Config Parser | 10 | Core INI parsing, includes, validation |
| Parser Robustness | 23 | Invalid inputs, edge cases, error handling |
| XML-RPC Parser | 24 | Malformed XML, data types, special characters |
| Integration | 7 | Full process lifecycle, multi-process |
| **TOTAL** | **64** | **100% Pass Rate** |

### Integration Test Scenarios

1. **Process Exits Immediately** - Tests startsecs validation
2. **Start/Stop/Restart Cycle** - Full lifecycle validation
3. **Multiple Processes** - Simultaneous process management
4. **Autorestart Failing Process** - Retry backoff to FATAL state
5. **Log Capture Validation** - Stdout/stderr capture correctness
6. **Working Directory** - Directory switching verification
7. **Rapid Start/Stop Cycles** - Stress test for race conditions

## Architecture

### Components

```
supervisorcpp/
├── include/
│   ├── config_types.h       # Configuration data structures
│   ├── config_parser.h      # INI configuration parser
│   ├── logger.h             # Logging infrastructure
│   ├── process.h            # Process lifecycle management
│   ├── process_manager.h    # Multi-process coordinator
│   ├── log_writer.h         # Log file rotation
│   └── rpc_server.h         # XML-RPC server
├── src/
│   ├── main.cpp             # supervisord entry point
│   ├── ctl/supervisorctl.cpp # supervisorctl client
│   ├── config/              # Configuration implementation
│   ├── process/             # Process management
│   ├── rpc/                 # RPC server
│   └── util/                # Utilities (logging)
└── tests/
    ├── config/              # Config parser tests (33 tests)
    ├── rpc/                 # XML-RPC parser tests (24 tests)
    └── integration/         # Full workflow tests (7 tests)
```

### Event Loop

supervisorcpp uses a single-threaded event loop powered by Boost.Asio:
- Async I/O for process stdout/stderr capture
- Signal handling (SIGCHLD for process exit detection)
- Timer-based periodic state updates
- Non-blocking socket I/O for RPC

### Process State Machine

```
STOPPED (0)
    ↓ start()
STARTING (10) ----+
    ↓             |
RUNNING (20)      | fail before startsecs
    ↓             |
STOPPING (40)     ↓
    ↓         BACKOFF (30) ---> retry (up to startretries)
EXITED (100)      ↓
                  | max retries exceeded
                  ↓
                FATAL (200)
```

## Performance

Optimizations for embedded environments:
- Single-threaded event loop (no thread overhead)
- Minimal dependencies (Boost only)
- Efficient async I/O (non-blocking)
- Static linking option available
- Small binary size (~2MB stripped)
- Low memory footprint
- Fast test execution (~0.15s for 64 tests)

## Differences from Python supervisord

### Implemented
- Core process management (start, stop, restart)
- Autorestart with retry backoff
- Log capture and rotation
- XML-RPC interface
- supervisorctl client (interactive & non-interactive)
- Signal handling
- Configuration validation

### Not Implemented (Minimal Version)
- Process groups
- Event listeners
- Web UI
- `numprocs` (multiple instances)
- Advanced autorestart options (unexpected)
- Full environment variable expansion (only `%(program_name)s`)
- HTTP authentication
- Remote RPC (only Unix sockets)

## Troubleshooting

### Common Issues

**supervisord fails to start**
- Check configuration file syntax: `supervisord -c config.ini` (will show parse errors)
- Verify log file directory exists and is writable
- Check socket file path is writable

**Process won't start**
- Check process user has permission to execute command
- Verify working directory exists
- Check stdout_logfile for error messages
- Increase `startsecs` if process takes time to initialize

**Process keeps restarting**
- Check process logs for crash reason
- Verify `startsecs` is appropriate for startup time
- Check `startretries` setting
- Look for FATAL state indicating max retries exceeded

**RPC connection failed**
- Verify socket file exists: `ls -la /var/run/supervisord.sock`
- Check socket file permissions
- Ensure supervisord is running: `ps aux | grep supervisord`

**Logs not rotating**
- Verify `stdout_logfile_maxbytes` setting
- Check disk space
- Ensure parent directory is writable

## License

This is a reference implementation for educational purposes.

## Contributing

Areas for contribution:
- Additional test coverage
- Performance profiling and optimization
- Alpine OpenRC integration script
- Static analysis (clang-tidy, cppcheck)
- Fuzzing tests for parsers
- Documentation improvements

## References

- [Supervisor Documentation](http://supervisord.org/)
- [XML-RPC Specification](http://xmlrpc.com/spec.md)
- [Boost.Asio Documentation](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html)
- [Alpine Linux](https://alpinelinux.org/)
