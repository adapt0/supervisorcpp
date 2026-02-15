#include "secure.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>

namespace supervisorcpp::util {

namespace fs = std::filesystem;

inline fs::path validate_canonicalize_path(const fs::path& path, const fs::path& allowed_prefix) {
    // Resolve to canonical path (resolves symlinks, . and ..)
    fs::path canonical;
    try {
        canonical = fs::canonical(path);
    } catch (const fs::filesystem_error& e) {
        // Path doesn't exist - try to canonicalize parent and append filename
        fs::path parent = path.parent_path();
        if (parent.empty()) parent = fs::current_path();

        const auto filename = path.filename();
        try {
            canonical = fs::canonical(parent) / filename;
        } catch (const fs::filesystem_error&) {
            throw SecurityError("Cannot resolve path: " + path.string());
        }
    }

    // Check if canonical path starts with allowed prefix
    const auto canonical_str = canonical.string();
    const auto prefix_str = allowed_prefix.string();
    if (canonical_str.find(prefix_str) != 0) {
        throw SecurityError("Path escapes allowed directory: " + path.string() +
                          " (resolved to: " + canonical_str +
                          ", allowed prefix: " + prefix_str + ")");
    }

    return canonical;
}

void validate_config_file_security(const fs::path& config_file) {
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

    // Must be owned by supervisorcpp's user (typically root - UID 0)
    if (st.st_uid != getuid()) {
        throw SecurityError("Config file must be owned by root (UID 0): " + config_file.string());
    }

    // Must not be world-writable (mode & 0002 == 0)
    if (st.st_mode & S_IWOTH) {
        throw SecurityError("Config file must not be world-writable: " + config_file.string());
    }

    // // Should not be group-writable for extra security
    // if (st.st_mode & S_IWGRP) {
    //     // Warning but not fatal
    //     std::cerr << "WARNING: Config file is group-writable: " + config_file.string() << std::endl;
    // }
}

fs::path validate_log_path(const fs::path& log_path) {
    // Allowed base directories for logs
    const fs::path allowed[] = {
        fs::weakly_canonical("/var/log"),
        fs::weakly_canonical("/tmp"),
        fs::weakly_canonical(fs::temp_directory_path()),
    };

    // Try to canonicalize against each allowed prefix
    for (const auto& prefix : allowed) {
        try {
            const auto canonical = validate_canonicalize_path(log_path, prefix);
            return canonical;
        } catch (const SecurityError&) {
            // std::cout << "validate_log_path " << e.what() << '\n';
        }
    }

    std::string allowed_str;
    for (const auto& a : allowed) {
        if (!allowed_str.empty()) allowed_str += " or ";
        allowed_str += a;
    }
    throw SecurityError("Log file must be under " + allowed_str + " - " + log_path.string());
}

void validate_command_path(const std::string& command) {
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

void validate_signal(const std::string& signal_name) {
    const std::string_view valid_signals[] = {
        "TERM", "HUP", "INT", "QUIT", "KILL", "USR1", "USR2", "ABRT", "ALRM", "CONT", "STOP"
    };

    if (std::find(std::begin(valid_signals), std::end(valid_signals), signal_name) == std::end(valid_signals)) {
        throw std::invalid_argument("Invalid signal name: " + signal_name);
    }
}

std::map<std::string, std::string> sanitize_environment(const std::map<std::string, std::string>& env) {
    std::map<std::string, std::string> sanitized;
    for (const auto& [key, value] : env) {
        // Validate key: alphanumeric + underscore only
        const bool valid_key = !key.empty() && std::all_of(std::begin(key), std::end(key), [](char c) {
            return std::isalnum(c) || c == '_';
        });

        if (!valid_key) {
            // std::cerr << "WARNING: Invalid environment variable name: " << key << std::endl;
            continue;
        }

        // Validate value: no null bytes
        if (value.find('\0') != std::string::npos) {
            // std::cerr << "WARNING: Environment value contains null byte: " << key << std::endl;
            continue;
        }

        sanitized[key] = value;
    }

    return sanitized;
}

void close_inherited_fds() {
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

void validate_socket_directory(const fs::path& socket_path) {
    const auto dir = socket_path.parent_path();

    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        throw SecurityError("Socket directory does not exist: " + dir.string());
    }

    if (!S_ISDIR(st.st_mode)) {
        throw SecurityError("Socket path parent is not a directory: " + dir.string());
    }

    // World-writable directories (even with sticky bit) are unsafe for sockets.
    // Sticky bit only prevents deletion of others' files, not creation of new
    // files at a recently-unlinked path — so TOCTOU between unlink() and bind()
    // allows DoS. Use a root-owned directory like /run/supervisord/ instead.
    if (st.st_mode & S_IWOTH) {
        throw SecurityError("Socket directory must not be world-writable: " + dir.string());
    }
}

void set_socket_permissions(const std::filesystem::path& socket_path) {
    if (chmod(socket_path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        throw SecurityError("Failed to set socket permissions: " + socket_path.string());
    }
}

void verify_privilege_drop(uid_t expected_uid, gid_t expected_gid) {
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

} // namespace supervisorcpp::util
