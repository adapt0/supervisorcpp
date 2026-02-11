#pragma once
#ifndef SUPERVISOR_LIB__LOGGER__LOGGER
#define SUPERVISOR_LIB__LOGGER__LOGGER

#include "log_levels.h"
#include <filesystem>
#include <boost/log/trivial.hpp>

namespace supervisorcpp::logger {

/**
 * Initialize the logging system
 * @param logfile Path to log file
 * @param level Log level
 */
void init_logging(const std::filesystem::path& logfile, LogLevel level);

/**
 * Shutdown the logging system
 */
void shutdown_logging();

/**
 * Set log level dynamically
 */
void set_log_level(LogLevel level);

} // namespace supervisorcpp::logger

// Convenient logging macros
#define LOG_TRACE   BOOST_LOG_TRIVIAL(trace)
#define LOG_DEBUG   BOOST_LOG_TRIVIAL(debug)
#define LOG_INFO    BOOST_LOG_TRIVIAL(info)
#define LOG_WARN    BOOST_LOG_TRIVIAL(warning)
#define LOG_ERROR   BOOST_LOG_TRIVIAL(error)

#endif // SUPERVISOR_LIB__LOGGER__LOGGER
