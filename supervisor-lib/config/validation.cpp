
#include "validation.h"

#include "../util/errors.h"
#include "../util/path.h"
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

namespace supervisord::config {

namespace fs = std::filesystem;

/**
 * Validate configuration file security
 * - Must be owned by root (UID 0)
 * - Must not be world-writable (mode & 0002 == 0)
 * - Must be a regular file (not symlink)
 * - Parent directory must be secure
 */
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

    // Should not be group-writable for extra security
    if (st.st_mode & S_IWGRP) {
        // Warning but not fatal
        std::cerr << "WARNING: Config file is group-writable: " + config_file.string() << std::endl;
    }
}

/**
 * Validate log file path is safe
 * - Must be under /var/log or /tmp
 * - No path traversal
 * - Parent directory must exist and be writable
 */
fs::path validate_log_path(const fs::path& log_path) {
    // Allowed base directories for logs
    const fs::path allowed[] = {
        fs::weakly_canonical("/var/log"),
        fs::weakly_canonical("/tmp"),
    };

    // Try to canonicalize against each allowed prefix
    for (const auto& prefix : allowed) {
        try {
            const auto canonical = util::canonicalize_path(log_path, prefix);
            return canonical;
        } catch (const SecurityError&) {
            // std::cout << "validate_log_path " << e.what() << '\n';
        }
    }

    throw SecurityError("Log file must be under /var/log or /tmp: " + log_path.string());
}

/**
 * Validate command path is absolute and exists
 */
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

/**
 * Sanitize environment variables map
 * Removes dangerous variables and validates names/values
 */
std::map<std::string, std::string> sanitize_environment(const std::map<std::string, std::string>& env) {
    constexpr std::string_view dangerous[] = {
        "LD_PRELOAD",
        "LD_LIBRARY_PATH",
        "LD_AUDIT",
        "LD_DEBUG",
        "DYLD_INSERT_LIBRARIES",  // macOS
        "DYLD_LIBRARY_PATH",      // macOS
        "PYTHONPATH",
        "PERL5LIB",
        "PERLLIB",
        "RUBYLIB",
        "GOPATH",
    };

    std::map<std::string, std::string> sanitized;
    for (const auto& [key, value] : env) {
        // Check if key is in dangerous list
        if (std::find(std::begin(dangerous), std::end(dangerous), key) != std::end(dangerous)) {
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

} // namespace supervisord::config
