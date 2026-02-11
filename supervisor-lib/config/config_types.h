#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__CONFIG_TYPES
#define SUPERVISOR_LIB__PROCESS__CONFIG_TYPES

#include "../logger/logger.h"
#include <map>
#include <optional>
#include <set>

namespace supervisorcpp::config {

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
    std::filesystem::path socket_file{"/run/supervisord.sock"};

    // Future: chmod, chown, username, password
};

/**
 * Configuration for supervisord section
 */
struct SupervisordConfig {
    std::filesystem::path logfile{"/var/log/supervisord.log"};
    logger::LogLevel loglevel{logger::LogLevel::INFO};
    std::string user{"root"};
    std::filesystem::path childlogdir{"/var/log/supervisor"};

    // Future: pidfile, umask, nodaemon, minfds, minprocs, etc.
};

/**
 * Configuration for supervisorctl section
 */
struct SupervisorctlConfig {
    std::string serverurl{"unix:///run/supervisord.sock"};

    // Future: username, password, prompt
};

/**
 * Configuration for a single program
 */
struct ProgramConfig {
    std::string name;                           // program name from [program:xxx]
    std::string command;                         // required
    std::map<std::string, std::string> environment; // KEY=value pairs
    std::optional<std::filesystem::path> directory; // working directory
    bool autorestart{true};
    std::string user{"root"};

    // Logging
    std::optional<std::filesystem::path> stdout_logfile;
    size_t stdout_logfile_maxbytes{50 * 1024 * 1024}; // 50MB default
    int stdout_logfile_backups{10};
    bool redirect_stderr{false};

    // Process control
    int startsecs{1};                          // seconds process must stay up
    int startretries{3};                       // number of startup attempts
    int stopwaitsecs{10};                      // SIGTERM to SIGKILL timeout
    std::string stopsignal{"TERM"};            // signal to send on stop

    // Future: priority, numprocs, stderr_logfile, etc.

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
    UnixHttpServerConfig unix_http_server;
    SupervisordConfig supervisord;
    SupervisorctlConfig supervisorctl;
    std::vector<ProgramConfig> programs;
    std::set<std::string> included; ///< track included files (loop check)

    /**
     * Find a program by name
     */
    const ProgramConfig* find_program(const std::string& name) const {
        for (const auto& prog : programs) {
            if (prog.name == name) {
                return &prog;
            }
        }
        return nullptr;
    }

    /**
     * Find a program by name (mutable)
     */
    ProgramConfig* find_program(const std::string& name) {
        for (auto& prog : programs) {
            if (prog.name == name) {
                return &prog;
            }
        }
        return nullptr;
    }
};

} // namespace supervisorcpp::config

#endif // SUPERVISOR_LIB__PROCESS__CONFIG_TYPES
