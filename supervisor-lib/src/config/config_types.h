// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__CONFIG_TYPES
#define SUPERVISOR_LIB__PROCESS__CONFIG_TYPES

#include "logger/logger.h"
#include <map>
#include <optional>
#include <set>
#include <sys/stat.h>

namespace supervisorcpp::config {

namespace fs = std::filesystem;

/**
 * Parse size string (e.g., "10MB", "1GB") to bytes
 */
inline size_t parse_size(const std::string& size_str) {
    if (size_str.empty()) return 0;

    size_t multiplier = 1;
    std::string num_str = size_str;

    // Check for suffix
    if (size_str.size() >= 2) {
        std::string suffix = size_str.substr(size_str.size() - 2);
        if (suffix == "KB" || suffix == "kb") {
            multiplier = 1024;
            num_str = size_str.substr(0, size_str.size() - 2);
        } else if (suffix == "MB" || suffix == "mb") {
            multiplier = 1024 * 1024;
            num_str = size_str.substr(0, size_str.size() - 2);
        } else if (suffix == "GB" || suffix == "gb") {
            multiplier = 1024 * 1024 * 1024;
            num_str = size_str.substr(0, size_str.size() - 2);
        }
    }

    try {
        // Validate that the number part only contains digits and optional whitespace
        std::string trimmed = num_str;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        if (trimmed.empty() || !std::all_of(std::begin(trimmed), std::end(trimmed), ::isdigit)) {
            throw std::invalid_argument("Invalid size format: " + size_str);
        }

        return std::stoull(trimmed) * multiplier;
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("Invalid size format: " + size_str);
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("Size value out of range: " + size_str);
    }
}

/**
 * Configuration for unix_http_server section
 */
struct UnixHttpServerConfig {
    fs::path socket_file{"/run/supervisord.sock"};

    // Future: chmod, chown, username, password
};

/**
 * Configuration for supervisord section
 */
struct SupervisordConfig {
    fs::path logfile{"/var/log/supervisord.log"};
    size_t logfile_maxbytes{50 * 1024 * 1024}; // 50MB default
    int logfile_backups{10};
    logger::LogLevel loglevel{logger::LogLevel::INFO};
    std::string user{"root"};
    fs::path childlogdir{"/var/log/supervisor"};
    fs::path pidfile;
    mode_t umask{022};
};

/**
 * Configuration for a single program
 */
struct ProgramConfig {
    std::string name;    // program name from [program:xxx]
    std::string command; // command to execute (required)
    std::map<std::string, std::string> environment; // KEY=value pairs
    std::optional<fs::path> directory; // working directory
    bool autorestart{true};
    std::string user{"root"};

    // Logging
    struct Log {
        std::optional<fs::path> file;
        size_t file_maxbytes{50 * 1024 * 1024}; // 50MB default
        int file_backups{10};

        bool operator==(const Log&) const = default;
    };
    Log stdout_log;
    Log stderr_log;
    bool redirect_stderr{false};

    // Process control
    int startsecs{1};                          // seconds process must stay up
    int startretries{3};                       // number of startup attempts
    int stopwaitsecs{10};                      // SIGTERM to SIGKILL timeout
    std::string stopsignal{"TERM"};            // signal to send on stop
    std::optional<mode_t> umask;               // per-process umask (applied before exec)

    bool operator==(const ProgramConfig&) const = default;

    /**
     * Substitute variables in a string (e.g., %(program_name)s)
     */
    std::string substitute_variables(const std::string& input) const {
        std::string result = input;

        // Replace %(program_name)s
        size_t pos = 0;
        while ((pos = result.find("%(program_name)s", pos)) != std::string::npos) {
            result.replace(pos, 16, name);
            pos += name.length();
        }

        return result;
    }
};

/**
 * Complete supervisord configuration
 */
struct Configuration {
    fs::path config_file;           ///< path to the config file (for reload)
    UnixHttpServerConfig unix_http_server;
    SupervisordConfig supervisord;
    std::vector<ProgramConfig> programs;
    std::set<std::string> included; ///< track included files (loop check)

};

} // namespace supervisorcpp::config

#endif // SUPERVISOR_LIB__PROCESS__CONFIG_TYPES
