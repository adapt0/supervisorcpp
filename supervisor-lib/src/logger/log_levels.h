// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__LOGGER__LOG_LEVELS
#define SUPERVISOR_LIB__LOGGER__LOG_LEVELS

#include <string>

namespace supervisorcpp::logger {

/**
 * Log level enumeration
 */
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    IGNORE, // ignore all errors (test default)
};

/**
 * Output operator for LogLevel
 */
std::ostream& operator<<(std::ostream& os, LogLevel level);

/**
 * Parse log level from string
 */
LogLevel parse_log_level(std::string level_str);

} // namespace supervisorcpp::logger

#endif // SUPERVISOR_LIB__LOGGER__LOG_LEVELS
