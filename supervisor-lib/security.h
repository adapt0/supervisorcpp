#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pwd.h>

namespace supervisord {
namespace security {

/**
 * Security exception for validation failures
 */
class SecurityError : public std::runtime_error {
public:
    explicit SecurityError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * Validate configuration file security
 * - Must be owned by root (UID 0)
 * - Must not be world-writable (mode & 0002 == 0)
 * - Must be a regular file (not symlink)
 * - Parent directory must be secure
 */
inline void validate_config_file_security(const std::filesystem::path& config_file) {
    namespace fs = std::filesystem;

    // Check file exists
    if (!fs::exists(config_file)) {
        throw SecurityError("Config file does not exist: " + config_file.string());
    }

    // Get file status (follows symlinks - we'll check for that)
    struct stat st;
    if (lstat(config_file.c_str(), &st) != 0) {
        throw SecurityError("Cannot stat config file: " + config_file.string());
    }

    // Must be regular file (not symlink, device, etc.)
    if (!S_ISREG(st.st_mode)) {
        throw SecurityError("Config file must be a regular file: " + config_file.string());
    }

    // Must be owned by root (UID 0)
    if (st.st_uid != 0) {
        throw SecurityError("Config file must be owned by root (UID 0): " + config_file.string());
    }

    // Must not be world-writable (mode & 0002 == 0)
    if (st.st_mode & S_IWOTH) {
        throw SecurityError("Config file must not be world-writable: " + config_file.string());
    }

    // Should not be group-writable for extra security
    if (st.st_mode & S_IWGRP) {
        // Warning but not fatal
        std::cerr << "WARNING: Config file is group-writable: " + config_file.string() << std::endl;
    }
}

/**
 * Canonicalize path and validate it doesn't escape allowed directory
 * Returns absolute path with symlinks resolved
 */
inline std::filesystem::path canonicalize_path(const std::filesystem::path& path,
                                                const std::filesystem::path& allowed_prefix) {
    namespace fs = std::filesystem;

    // Resolve to canonical path (resolves symlinks, . and ..)
    fs::path canonical;
    try {
        canonical = fs::canonical(path);
    } catch (const fs::filesystem_error& e) {
        // Path doesn't exist - try to canonicalize parent and append filename
        fs::path parent = path.parent_path();
        fs::path filename = path.filename();

        if (parent.empty()) {
            parent = fs::current_path();
        }

        try {
            canonical = fs::canonical(parent) / filename;
        } catch (const fs::filesystem_error&) {
            throw SecurityError("Cannot resolve path: " + path.string());
        }
    }

    // Check if canonical path starts with allowed prefix
    std::string canonical_str = canonical.string();
    std::string prefix_str = allowed_prefix.string();

    if (canonical_str.find(prefix_str) != 0) {
        throw SecurityError("Path escapes allowed directory: " + path.string() +
                          " (resolved to: " + canonical_str +
                          ", allowed prefix: " + prefix_str + ")");
    }

    return canonical;
}

/**
 * Validate log file path is safe
 * - Must be under /var/log or /tmp
 * - No path traversal
 * - Parent directory must exist and be writable
 */
inline std::filesystem::path validate_log_path(const std::filesystem::path& log_path) {
    namespace fs = std::filesystem;

    // Allowed base directories for logs
    std::vector<std::string> allowed_prefixes = {"/var/log", "/tmp"};

    // Try to canonicalize against each allowed prefix
    fs::path canonical;
    bool valid = false;

    for (const auto& prefix : allowed_prefixes) {
        try {
            canonical = canonicalize_path(log_path, prefix);
            valid = true;
            break;
        } catch (const SecurityError&) {
            // Try next prefix
            continue;
        }
    }

    if (!valid) {
        throw SecurityError("Log file must be under /var/log or /tmp: " + log_path.string());
    }

    return canonical;
}

/**
 * Dangerous environment variables that should be filtered
 */
inline const std::vector<std::string>& get_dangerous_env_vars() {
    static const std::vector<std::string> dangerous = {
        "LD_PRELOAD",
        "LD_LIBRARY_PATH",
        "LD_AUDIT",
        "LD_DEBUG",
        "DYLD_INSERT_LIBRARIES",  // macOS
        "DYLD_LIBRARY_PATH",       // macOS
        "PYTHONPATH",
        "PERL5LIB",
        "PERLLIB",
        "RUBYLIB",
        "GOPATH",
    };
    return dangerous;
}

/**
 * Sanitize environment variables map
 * Removes dangerous variables and validates names/values
 */
inline std::map<std::string, std::string> sanitize_environment(
    const std::map<std::string, std::string>& env) {

    std::map<std::string, std::string> sanitized;
    const auto& dangerous = get_dangerous_env_vars();

    for (const auto& [key, value] : env) {
        // Check if key is in dangerous list
        if (std::find(dangerous.begin(), dangerous.end(), key) != dangerous.end()) {
            std::cerr << "WARNING: Filtering dangerous environment variable: " << key << std::endl;
            continue;
        }

        // Validate key: alphanumeric + underscore only
        bool valid_key = !key.empty() && std::all_of(key.begin(), key.end(), [](char c) {
            return std::isalnum(c) || c == '_';
        });

        if (!valid_key) {
            std::cerr << "WARNING: Invalid environment variable name: " << key << std::endl;
            continue;
        }

        // Validate value: no null bytes
        if (value.find('\0') != std::string::npos) {
            std::cerr << "WARNING: Environment value contains null byte: " << key << std::endl;
            continue;
        }

        sanitized[key] = value;
    }

    return sanitized;
}

/**
 * Set secure permissions on Unix socket
 * chmod 0600 (owner read/write only)
 */
inline void set_socket_permissions(const std::filesystem::path& socket_path) {
    if (chmod(socket_path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        throw SecurityError("Failed to set socket permissions: " + socket_path.string());
    }
}

/**
 * Verify privilege drop was successful
 * Checks that we're running as the target UID/GID
 */
inline void verify_privilege_drop(uid_t expected_uid, gid_t expected_gid) {
    uid_t real_uid = getuid();
    uid_t effective_uid = geteuid();
    gid_t real_gid = getgid();
    gid_t effective_gid = getegid();

    if (real_uid != expected_uid || effective_uid != expected_uid) {
        throw SecurityError("Privilege drop failed: UID mismatch (expected " +
                          std::to_string(expected_uid) +
                          ", got real=" + std::to_string(real_uid) +
                          ", effective=" + std::to_string(effective_uid) + ")");
    }

    if (real_gid != expected_gid || effective_gid != expected_gid) {
        throw SecurityError("Privilege drop failed: GID mismatch (expected " +
                          std::to_string(expected_gid) +
                          ", got real=" + std::to_string(real_gid) +
                          ", effective=" + std::to_string(effective_gid) + ")");
    }
}

/**
 * Validate command path is absolute and exists
 */
inline void validate_command_path(const std::string& command) {
    // Extract first token (the actual command path)
    std::string cmd_path = command;
    size_t space_pos = cmd_path.find(' ');
    if (space_pos != std::string::npos) {
        cmd_path = cmd_path.substr(0, space_pos);
    }

    // Must be absolute path for security
    if (cmd_path.empty() || cmd_path[0] != '/') {
        throw SecurityError("Command must be an absolute path: " + command);
    }

    // Check if executable exists
    struct stat st;
    if (stat(cmd_path.c_str(), &st) != 0) {
        throw SecurityError("Command not found: " + cmd_path);
    }

    // Must be a regular file or symlink to one
    if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) {
        throw SecurityError("Command must be a regular file: " + cmd_path);
    }

    // Check if executable
    if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        throw SecurityError("Command is not executable: " + cmd_path);
    }
}

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
inline void close_inherited_fds() {
    // Get maximum number of file descriptors
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        // Close all fds from 3 to max (keeping 0=stdin, 1=stdout, 2=stderr)
        int max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : static_cast<int>(rl.rlim_max);
        for (int fd = 3; fd < max_fd; fd++) {
            close(fd);  // Ignore errors (fd may not be open)
        }
    }
}

/**
 * Set resource limits for child process
 */
inline void set_child_resource_limits() {
    struct rlimit rlim;

    // Limit number of processes (prevent fork bombs)
    rlim.rlim_cur = 100;  // Soft limit
    rlim.rlim_max = 200;  // Hard limit
    setrlimit(RLIMIT_NPROC, &rlim);

    // Limit address space to 4GB (prevent memory exhaustion)
    rlim.rlim_cur = 4UL * 1024 * 1024 * 1024;
    rlim.rlim_max = 4UL * 1024 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &rlim);

    // Limit core dump size to 0 (no core dumps)
    rlim.rlim_cur = 0;
    rlim.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &rlim);
}

} // namespace security
} // namespace supervisord
