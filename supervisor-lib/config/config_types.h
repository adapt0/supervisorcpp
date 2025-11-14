#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace supervisord {
namespace config {

/**
 * Log level enumeration
 */
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

/**
 * Process state enumeration (supervisord compatible)
 */
enum class ProcessState {
    STOPPED = 0,
    STARTING = 10,
    RUNNING = 20,
    BACKOFF = 30,
    STOPPING = 40,
    EXITED = 100,
    FATAL = 200
};

/**
 * Parse size string (e.g., "10MB", "1GB") to bytes
 */
inline size_t parse_size(const std::string& size_str) {
    if (size_str.empty()) {
        return 0;
    }

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

        if (trimmed.empty() || !std::all_of(trimmed.begin(), trimmed.end(), ::isdigit)) {
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
 * Parse log level from string
 */
inline LogLevel parse_log_level(const std::string& level_str) {
    std::string lower = level_str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "debug") return LogLevel::DEBUG;
    if (lower == "info") return LogLevel::INFO;
    if (lower == "warn" || lower == "warning") return LogLevel::WARN;
    if (lower == "error") return LogLevel::ERROR;

    // Invalid log level
    throw std::invalid_argument("Invalid log level: " + level_str);
}

/**
 * Validate signal name
 */
inline void validate_signal(const std::string& signal_name) {
    const std::vector<std::string> valid_signals = {
        "TERM", "HUP", "INT", "QUIT", "KILL", "USR1", "USR2", "ABRT", "ALRM", "CONT", "STOP"
    };

    if (std::find(valid_signals.begin(), valid_signals.end(), signal_name) == valid_signals.end()) {
        throw std::invalid_argument("Invalid signal name: " + signal_name);
    }
}

/**
 * Convert log level to string
 */
inline std::string log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO: return "info";
        case LogLevel::WARN: return "warn";
        case LogLevel::ERROR: return "error";
        default: return "info";
    }
}

/**
 * Output operator for LogLevel (for testing and debugging)
 */
inline std::ostream& operator<<(std::ostream& os, LogLevel level) {
    return os << log_level_to_string(level);
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
    LogLevel loglevel{LogLevel::INFO};
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

} // namespace config
} // namespace supervisord
