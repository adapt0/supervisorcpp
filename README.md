# supervisorcpp

A lightweight process supervisor written in C++23 for embedded Linux environments (Alpine Linux with musl). Drop-in replacement for supervisord — compatible with its configuration files, CLI tools, and XML-RPC interface.

## Quick Start

```bash
# Build
mkdir build && cd build
cmake .. -G Ninja
ninja -j$(nproc)

# Run tests
ctest --output-on-failure

# Install
sudo ninja install
```

## Usage

supervisorcpp uses a **busybox-style multi-call binary**. A single `supervisor` binary dispatches by `argv[0]` or subcommand:

```bash
# Start the daemon
supervisord -c /etc/supervisord.conf
supervisor -c /etc/supervisord.conf      # equivalent (daemon is the default mode)

# Control processes
supervisorctl status
supervisorctl start myapp
supervisorctl stop myapp
supervisorctl restart myapp
supervisorctl shutdown
supervisor ctl status                     # equivalent via subcommand
```

> **Note:** Unlike Python supervisord, supervisorcpp always runs in the foreground.
> Use your init system (OpenRC, systemd, Docker) to manage it as a service.
> The `-n`/`--nodaemon` flag is accepted for compatibility but is a no-op.

### Interactive Mode

```bash
$ supervisorctl
supervisor> status
myapp        RUNNING    pid 1234, uptime 0:05:23
webapp       STOPPED

supervisor> help
status          Show process status
start <name>    Start a process
stop <name>     Stop a process
restart <name>  Restart a process
shutdown        Shutdown supervisord
reload          Reload configuration
help            Show this help
exit            Exit supervisorctl
```

## Configuration

Compatible with standard supervisord INI format:

```ini
[unix_http_server]
file=/var/run/supervisord.sock

[supervisord]
logfile=/var/log/supervisord.log
loglevel=info

[program:myapp]
command=/usr/local/bin/myapp --config /etc/myapp.conf
directory=/var/lib/myapp
user=appuser
autorestart=true
stdout_logfile=/var/log/%(program_name)s.log
stdout_logfile_maxbytes=50MB
startsecs=5
startretries=3
stopwaitsecs=10
stopsignal=TERM
environment=PATH="/usr/local/bin:/usr/bin",CONFIG_ENV="production"
```

### Configuration Reference

#### [unix_http_server]
| Option | Default | Description |
|--------|---------|-------------|
| `file` | `/run/supervisord.sock` | Unix domain socket path |

#### [supervisord]
| Option | Default | Description |
|--------|---------|-------------|
| `childlogdir` | `/var/log/supervisor` | Directory for AUTO child logs |
| `logfile` | `/var/log/supervisord.log` | Main log file |
| `loglevel` | `info` | Log level: debug, info, warn, error |
| `umask` | `022` | File creation mask (octal) |
| `user` | `root` | Run daemon as user |

#### [program:x]
| Option | Default | Description |
|--------|---------|-------------|
| `autorestart` | `true` | Restart on exit |
| `command` | *(required)* | Command to run |
| `directory` | — | Working directory |
| `environment` | — | Env vars: `KEY1="val1",KEY2="val2"` |
| `redirect_stderr` | `false` | Merge stderr into stdout log |
| `startretries` | `3` | Max restart attempts before FATAL |
| `startsecs` | `1` | Seconds before considered started |
| `stderr_logfile_backups` | `10` | Rotated stderr log files to keep |
| `stderr_logfile_maxbytes` | `50MB` | Max stderr log size before rotation |
| `stderr_logfile` | — | Stderr log path (supports `%(program_name)s`) |
| `stdout_logfile_backups` | `10` | Rotated log files to keep |
| `stdout_logfile_maxbytes` | `50MB` | Max log size before rotation |
| `stdout_logfile` | — | Stdout log path (supports `%(program_name)s`) |
| `stopsignal` | `TERM` | Signal on stop: TERM, HUP, INT, QUIT, KILL, USR1, USR2 |
| `stopwaitsecs` | `10` | Seconds before SIGKILL after stop signal |
| `umask` | — | Per-process file creation mask (octal) |
| `user` | `root` | Run process as user |

#### [include]
| Option | Description |
|--------|-------------|
| `files` | Glob patterns for additional config files (e.g. `conf.d/*.ini`) |

## Features

- **Process management** — start, stop, restart with automatic retry and exponential backoff to FATAL state
- **Process states** — STOPPED, STARTING, RUNNING, BACKOFF, STOPPING, EXITED, FATAL
- **Signal handling** — configurable stop signal, graceful SIGTERM-then-SIGKILL shutdown
- **Log capture** — async stdout/stderr capture with size-based rotation on line boundaries
- **Configuration** — supervisord-compatible INI format with include files, glob patterns, and `%(program_name)s` substitution
- **XML-RPC** — full control API over Unix domain sockets
- **supervisorctl** — interactive and non-interactive modes with formatted output

## Building

### Requirements

- C++23 compiler (GCC 13+ or Clang 16+)
- CMake 3.20+
- Boost 1.80+ (log, filesystem, system, asio, property_tree, program_options, unit_test_framework)

```bash
# Alpine Linux
apk add cmake g++ boost-dev boost1.84-static ninja

# Ubuntu/Debian
apt-get install cmake g++ libboost-all-dev ninja-build
```

## Differences from Python supervisord

### Supported
- Core process management (start, stop, restart, autorestart)
- Log capture and size-based rotation
- XML-RPC interface over Unix sockets
- supervisorctl (interactive and non-interactive)
- Signal handling, configuration validation, include files

### Not Supported
- Daemonization (use your init system)
- Process groups and priorities
- Event listeners
- Web UI
- `numprocs` (multiple instances)
- `autorestart=unexpected`
- HTTP/TCP servers or authentication
- Variable expansion beyond `%(program_name)s`

## Troubleshooting

**supervisord fails to start**
- Check config syntax: `supervisord -c config.ini` (shows parse errors)
- Verify log directory exists and is writable
- Check socket path is writable

**Process won't start**
- Check user has permission to execute command
- Verify working directory exists
- Check stdout_logfile for errors
- Increase `startsecs` if process needs time to initialize

**Process keeps restarting**
- Check process logs for crash reason
- Verify `startsecs` is appropriate
- Look for FATAL state indicating max retries exceeded

**RPC connection failed**
- Verify socket file exists: `ls -la /var/run/supervisord.sock`
- Check socket permissions
- Ensure supervisord is running

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — design decisions, state machine, extension guides
- [Specification](docs/SPECIFICATION.md) — project specification, threat model
- [TODO](docs/TODO.md) — known issues and future work

## References

- [Supervisor Documentation](http://supervisord.org/)
- [XML-RPC Specification](http://xmlrpc.com/spec.md)
- [Boost](https://www.boost.org)
