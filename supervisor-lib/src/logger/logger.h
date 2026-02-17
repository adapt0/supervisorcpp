// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__LOGGER__LOGGER
#define SUPERVISOR_LIB__LOGGER__LOGGER

#include "log_levels.h"
#include <filesystem>
#include <string_view>
#include <boost/log/trivial.hpp>

namespace supervisorcpp::logger {

/**
 * Initialize the logging system
 */
void init_logging(LogLevel level = LogLevel::INFO);

/**
 * Initialize logging to file
 * @param logfile Path to log file
 * @param level Log level
 */
void init_file_logging(const std::filesystem::path& logfile, LogLevel level,
                       size_t max_bytes = 50 * 1024 * 1024, int backups = 10,
                       std::string_view header = "");

/**
 * Shutdown the logging system
 */
void shutdown_logging();

/**
 * Get current log level
 */
LogLevel get_log_level();

/**
 * Set log level dynamically
 */
void set_log_level(LogLevel level);

/**
 * Shift log level by an increment (verbosity adjustment)
 */
void increment_log_level(int amount = 1);

} // namespace supervisorcpp::logger

// Convenient logging macros
#define LOG_TRACE   BOOST_LOG_TRIVIAL(trace)
#define LOG_DEBUG   BOOST_LOG_TRIVIAL(debug)
#define LOG_INFO    BOOST_LOG_TRIVIAL(info)
#define LOG_WARN    BOOST_LOG_TRIVIAL(warning)
#define LOG_ERROR   BOOST_LOG_TRIVIAL(error)

#endif // SUPERVISOR_LIB__LOGGER__LOGGER
