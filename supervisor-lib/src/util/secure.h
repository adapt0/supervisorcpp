#pragma once
#ifndef SUPERVISOR_LIB__UTIL__SECURE
#define SUPERVISOR_LIB__UTIL__SECURE

#include <filesystem>
#include <map>
#include <span>
#include <unistd.h>

namespace supervisorcpp::util {

namespace fs = std::filesystem;

/**
 * Security validation exception
 * Thrown when security checks fail (ownership, permissions, paths, etc.)
 */
class SecurityError : public std::runtime_error {
public:
    explicit SecurityError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * Canonicalize path and validate it doesn't escape allowed directory
 * Returns absolute path with symlinks resolved
 */
fs::path validate_canonicalize_path(const fs::path& path, const fs::path& allowed_prefix);

/**
 * Validate configuration file security
 * - Must be owned by current uid (i.e., root UID 0)
 * - Must not be world-writable (mode & 0002 == 0)
 * - Must be a regular file (not symlink)
 * - Parent directory must be secure
 */
void validate_config_file_security(const fs::path& config_file);

// Helper: validate path is under one of the allowed directories
fs::path validate_path_in_allowed_dirs(const fs::path& path, std::span<const fs::path> allowed_dirs, const std::string& description);

/**
 * Validate log file path is safe
 * - Must be under /var/log or /tmp
 * - No path traversal
 * - Parent directory must exist and be writable
 */
fs::path validate_log_path(const fs::path& log_path);

/**
 * Validate pidfile path is safe
 * - Must be under /run, /var/run, or /tmp
 * - No path traversal
 * - Must be absolute path
 */
fs::path validate_pidfile_path(const fs::path& pidfile_path);

/**
 * Validate command path is absolute and exists
 */
void validate_command_path(const std::string& command);

/**
 * Validate signal name
 */
void validate_signal(const std::string& signal_name);

/**
 * Sanitize environment variables map
 * Validates variable names (alphanumeric + underscore) and values (no null bytes)
 */
std::map<std::string, std::string> sanitize_environment(const std::map<std::string, std::string>& env);

/**
 * Close all file descriptors except stdin/stdout/stderr
 * This prevents child processes from inheriting parent's file descriptors
 * (sockets, log files, etc.) which could cause:
 * - File descriptor leaks
 * - Child processes interfering with parent's files/sockets
 * - Security issues if child is compromised
 *
 * Note: This should be called AFTER stdout/stderr have been redirected
 * in the child process, but BEFORE exec.
 */
void close_inherited_fds();

/**
 * Validate socket directory is not world-writable
 * Prevents TOCTOU attacks between unlink and bind in shared directories
 */
void validate_socket_directory(const fs::path& socket_path);

/**
 * Set secure permissions on Unix socket
 * chmod 0600 (owner read/write only)
 */
void set_socket_permissions(const fs::path& socket_path);

/**
 * Verify privilege drop was successful
 * Checks that we're running as the target UID/GID
 */
void verify_privilege_drop(uid_t expected_uid, gid_t expected_gid);

} // namespace supervisorcpp::util

#endif // SUPERVISOR_LIB__UTIL__SECURE
