# Supervisord C++ Minimal Replacement - Specification

## 1. Overview

### 1.1 Purpose
Replace supervisord in an embedded Alpine Linux environment to eliminate Python runtime dependency while maintaining configuration and CLI compatibility.

### 1.2 Design Goals
- Minimal binary size and runtime footprint
- Compatible with existing supervisord configuration files
- Support supervisorctl command-line interface
- Suitable for embedded systems (Alpine Linux + OpenRC)
- Standard C++ + Boost, minimize external dependencies

### 1.3 Scope Limitations
This is a **minimal** replacement focusing on:
- Single-instance processes (no numprocs)
- Individual process control (no process groups)
- Simple autorestart behavior (no "unexpected" mode)
- Size-based log rotation only
- Configuration reload via restart only

## 2. Functional Requirements

### 2.1 Process Management

#### 2.1.1 Process Lifecycle
- **Spawn** processes with configured command, environment, working directory, and user
- **Monitor** process state (STOPPED, STARTING, RUNNING, STOPPING, BACKOFF, FATAL)
- **Restart** automatically on exit when `autorestart=true`
- **Backoff** retry logic with exponential backoff on repeated failures
- **Graceful shutdown** using SIGTERM with timeout, then SIGKILL

#### 2.1.2 Process State Machine
```
STOPPED -> STARTING -> RUNNING -> STOPPING -> STOPPED
              |            |
              v            v
           BACKOFF      EXITED
              |
              v
           FATAL (after max retries)
```

#### 2.1.3 Default Behavior (supervisord compatibility)
- `startsecs`: 1 second (process must run this long to be considered started)
- `startretries`: 3 attempts
- `stopwaitsecs`: 10 seconds (SIGTERM to SIGKILL timeout)
- `stopsignal`: TERM
- `autorestart`: true (default)

### 2.2 Configuration

#### 2.2.1 Required Configuration Sections

**[unix_http_server]**
- `file`: Unix socket path for RPC communication

**[supervisord]**
- `logfile`: Main supervisord log file path
- `loglevel`: info, debug, warn, error
- `user`: User to run supervisord as (must be root to support per-process users)
- `childlogdir`: Directory for child process logs

**[rpcinterface:supervisor]**
- Required for supervisorctl compatibility (may be simplified in implementation)

**[supervisorctl]**
- `serverurl`: Unix socket URL (unix:///path/to/socket)

**[include]**
- `files`: Glob patterns for additional configuration files (*.ini)

**[program:name]**
- `command`: Command line to execute (required)
- `environment`: Environment variables (KEY=value,KEY2=value2)
- `directory`: Working directory
- `autorestart`: true/false (default: true)
- `user`: User to run process as
- `stdout_logfile`: Log file path with %(program_name)s substitution
- `stdout_logfile_maxbytes`: Max log size (supports KB, MB, GB suffixes)
- `redirect_stderr`: Redirect stderr to stdout (default: false)

#### 2.2.2 Variable Substitution
- `%(program_name)s`: Program name from [program:xxx]

#### 2.2.3 Configuration Parsing
- INI format using standard parsing library
- Support for multiple `files` directives in [include]
- Glob pattern expansion (*.ini)

### 2.3 Logging

#### 2.3.1 Supervisord Main Log
- Configurable log level
- Rotation based on size (when implemented)
- Log supervisord events: startup, shutdown, process state changes

#### 2.3.2 Child Process Logs
- Capture stdout (and stderr if redirect_stderr=true)
- Line-buffered output
- Size-based rotation on line boundaries
- Format: `stdout_logfile_maxbytes` (e.g., "10MB")
- Keep rotated logs as `.log.1`, `.log.2`, etc.

### 2.4 RPC Interface (supervisorctl compatibility)

#### 2.4.1 Required RPC Methods
- `supervisor.getState()`: Get supervisord state
- `supervisor.getAllProcessInfo()`: Get all process information
- `supervisor.getProcessInfo(name)`: Get single process info
- `supervisor.startProcess(name)`: Start a process
- `supervisor.stopProcess(name)`: Stop a process
- `supervisor.startAllProcesses()`: Start all processes
- `supervisor.stopAllProcesses()`: Stop all processes
- `supervisor.restart()`: Restart supervisord
- `supervisor.shutdown()`: Shutdown supervisord

#### 2.4.2 RPC Transport
- XML-RPC over Unix domain socket (supervisord standard)
- HTTP server listening on configured socket path
- Minimal XML-RPC implementation (may use lightweight library)

#### 2.4.3 Process Info Structure
```
{
    'name': 'process_name',
    'group': 'process_name',  // same as name (no groups)
    'statename': 'RUNNING',   // STOPPED, STARTING, RUNNING, BACKOFF, FATAL
    'state': 20,              // numeric state code
    'pid': 12345,
    'exitstatus': 0,
    'stdout_logfile': '/path/to/log',
    'stderr_logfile': '',
    'spawnerr': '',           // error message if spawn failed
    'now': 1234567890,        // current time
    'start': 1234567880,      // start time
    'stop': 0,                // stop time (0 if running)
    'description': 'pid 12345, uptime 0:00:10'
}
```

### 2.5 supervisorctl Commands

#### 2.5.1 Required Commands
- `status [name]`: Show process status (all or specific)
- `start <name|all>`: Start process(es)
- `stop <name|all>`: Stop process(es)
- `restart <name|all>`: Restart process(es)
- `shutdown`: Shutdown supervisord
- `reload`: Reload configuration (restarts supervisord)
- `help`: Show available commands
- `exit/quit`: Exit supervisorctl

#### 2.5.2 Command Output Format
Match supervisord output format for compatibility with scripts:
```
program_name    RUNNING   pid 12345, uptime 0:01:23
```

## 3. Technical Architecture

### 3.1 Components

```
┌─────────────────────────────────────────┐
│         supervisord (main daemon)        │
├─────────────────────────────────────────┤
│  - Configuration Parser                  │
│  - Process Manager                       │
│  - Event Loop (epoll/kqueue)            │
│  - RPC Server (Unix socket)             │
│  - Log Manager                           │
└─────────────────────────────────────────┘
              │
              │ Unix Socket
              │
┌─────────────────────────────────────────┐
│      supervisorctl (CLI client)          │
├─────────────────────────────────────────┤
│  - RPC Client                            │
│  - Command Parser                        │
│  - Output Formatter                      │
└─────────────────────────────────────────┘
```

### 3.2 Technology Stack

- **Language**: C++23
- **C Library**: musl (Alpine Linux)
- **Build System**: CMake
- **Core Libraries**:
  - Standard Library (filesystem, threads, chrono, etc.)
  - **Boost.PropertyTree**: INI configuration parsing and XML-RPC serialization
  - **Boost.Log**: Logging framework with built-in rotation support
  - **Boost.Asio**: Event loop, async I/O, Unix domain socket server
  - **Boost.Process**: Process spawning utilities (supplemented with raw fork/exec for setuid/setgid)
  - **Boost.Test**: Unit testing framework
- **Process Management**: Manual fork/exec for fine-grained control (user switching, file descriptors)

### 3.3 Process Monitoring

Use Boost.Asio signal handling for SIGCHLD to detect process exits asynchronously. Supplement with `waitpid()` with WNOHANG to reap zombie processes and determine exit status.

### 3.4 User Switching

Use `setuid()`, `setgid()`, and `initgroups()` to drop privileges per-process. Supervisord must run as root.

### 3.5 Log Rotation

Use Boost.Log's rotation capabilities configured for size-based rotation:
1. Set rotation size based on `stdout_logfile_maxbytes`
2. Rotate on line boundaries (text sink with auto-flush)
3. Archive rotated logs with numbered suffixes (.log.1, .log.2, etc.)
4. Limit number of archived logs (default: 10)

## 4. Implementation Phases

### Phase 1: Configuration & Core Framework
**Goal**: Parse configuration and establish basic structure
- INI configuration parser
- Configuration data structures
- Basic logging framework
- Main daemon skeleton
- Unit tests for configuration parsing

**Deliverable**: Loads and validates configuration files

### Phase 2: Process Management
**Goal**: Launch and monitor processes
- Process spawning (fork/exec)
- User switching (setuid/setgid)
- Environment and working directory setup
- Process state tracking
- Signal handling (SIGCHLD)
- Event loop (epoll)
- Basic autorestart logic

**Deliverable**: Spawns processes and restarts them on exit

### Phase 3: Lifecycle & Logging
**Goal**: Complete process lifecycle and output capture
- Process state machine (STARTING, BACKOFF, FATAL, etc.)
- Startup retry logic with backoff
- Graceful shutdown (SIGTERM -> SIGKILL)
- stdout/stderr capture via pipes
- Log file writing
- Size-based log rotation

**Deliverable**: Full process lifecycle with logging

### Phase 4: RPC Interface
**Goal**: Enable remote control via Unix socket
- Unix domain socket server
- HTTP request parsing (minimal)
- XML-RPC protocol handling
- Implement required RPC methods
- RPC error handling

**Deliverable**: supervisorctl can query status and control processes

### Phase 5: supervisorctl Client
**Goal**: Command-line interface
- Command parser
- RPC client implementation
- Output formatting (match supervisord format)
- Interactive and non-interactive modes
- Error handling and user feedback

**Deliverable**: Feature-complete supervisorctl replacement

### Phase 6: Polish & Production Readiness
**Goal**: Production quality
- Comprehensive error handling
- Edge case testing
- Signal handling (SIGTERM, SIGHUP, etc.)
- Daemon mode (double fork, setsid)
- PID file support
- Integration testing
- Documentation

**Deliverable**: Production-ready system

## 5. Configuration Compatibility

### 5.1 Supported supervisord.conf Options

| Section | Option | Support | Notes |
|---------|--------|---------|-------|
| [unix_http_server] | file | ✓ | Unix socket path |
| [supervisord] | logfile | ✓ | Main log file |
| [supervisord] | loglevel | ✓ | info, debug, warn, error |
| [supervisord] | user | ✓ | Run as user (must be root) |
| [supervisord] | childlogdir | ✓ | Child log directory |
| [rpcinterface:supervisor] | * | ✓ | Required but may be no-op |
| [supervisorctl] | serverurl | ✓ | Unix socket URL |
| [include] | files | ✓ | Glob patterns |
| [program:x] | command | ✓ | Required |
| [program:x] | environment | ✓ | KEY=value format |
| [program:x] | directory | ✓ | Working directory |
| [program:x] | autorestart | ✓ | true/false only |
| [program:x] | user | ✓ | Run as user |
| [program:x] | stdout_logfile | ✓ | With %(program_name)s |
| [program:x] | stdout_logfile_maxbytes | ✓ | Size-based rotation |
| [program:x] | redirect_stderr | ✓ | Merge stderr to stdout |

### 5.2 Unsupported Features (out of scope)
- TCP/HTTP servers (inet_http_server)
- Process groups and priorities
- numprocs (multiple instances)
- Event listeners and notifications
- autorestart=unexpected
- Environment variable expansion beyond %(program_name)s
- Advanced stdout/stderr options (backlog, capture_maxbytes)

## 6. Future Extensions

### 6.1 Potential Enhancements
- Resource limits (CPU, memory) per process via cgroups
- Systemd integration improvements
- Metrics/monitoring endpoint
- Configuration validation tool
- Hot configuration reload
- More sophisticated retry backoff strategies

### 6.2 Performance Targets
- Binary size: < 500KB (stripped)
- Memory footprint: < 5MB resident (managing 10 processes)
- Startup time: < 100ms
- Process spawn latency: < 50ms

## 7. Testing Strategy

### 7.1 Unit Tests
- Configuration parsing (various valid/invalid configs)
- State machine transitions
- Log rotation logic
- Variable substitution

### 7.2 Integration Tests
- End-to-end process lifecycle
- supervisorctl commands
- Configuration reload
- Crash recovery
- Log rotation under load

### 7.3 Compatibility Tests
- Drop-in replacement test with actual embedded system configs
- supervisorctl compatibility (Python client against C++ server)

## 8. Deployment

### 8.1 OpenRC Integration
- Init script in `/etc/init.d/supervisord`
- Start on boot: `rc-update add supervisord default`
- Standard OpenRC commands: `rc-service supervisord start|stop|restart`

### 8.2 Installation
- Single binary: `/usr/local/bin/supervisord`
- CLI tool: `/usr/local/bin/supervisorctl`
- Default config: `/etc/supervisord.conf`
- Systemd compatibility layer (if needed)

---

## Appendix A: State Codes

| State | Code | Description |
|-------|------|-------------|
| STOPPED | 0 | Process not started |
| STARTING | 10 | Process starting (before startsecs) |
| RUNNING | 20 | Process running successfully |
| BACKOFF | 30 | Process exited, will retry |
| STOPPING | 40 | Process being stopped |
| EXITED | 100 | Process exited (autorestart=false) |
| FATAL | 200 | Process failed after max retries |

## Appendix B: Default Values

```ini
; Process defaults (if not specified)
autorestart=true
startsecs=1
startretries=3
stopwaitsecs=10
stopsignal=TERM
redirect_stderr=false
stdout_logfile_maxbytes=50MB
stdout_logfile_backups=10
```

## Appendix C: Example Minimal Implementation

Suggested file structure:
```
supervisorcpp/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                 # supervisord entry point
│   ├── config/
│   │   ├── config_parser.h/cpp  # INI parsing
│   │   └── config_types.h       # Configuration structures
│   ├── process/
│   │   ├── process_manager.h/cpp  # Process lifecycle
│   │   ├── process.h/cpp          # Single process
│   │   └── log_writer.h/cpp       # Log management
│   ├── rpc/
│   │   ├── rpc_server.h/cpp     # XML-RPC server
│   │   └── rpc_methods.h/cpp    # RPC method handlers
│   ├── util/
│   │   ├── logger.h/cpp         # Logging utilities
│   │   └── event_loop.h/cpp     # epoll wrapper
│   └── ctl/
│       └── supervisorctl.cpp    # CLI client
├── tests/
│   └── ...                      # Unit/integration tests
└── docs/
    └── SPECIFICATION.md         # This document
```
