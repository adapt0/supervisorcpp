#pragma once

#include "../config/config_types.h"
#include <boost/log/trivial.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <filesystem>
#include <string>

namespace supervisord {
namespace util {

/**
 * Initialize the logging system
 * @param logfile Path to log file
 * @param level Log level
 */
void init_logging(const std::filesystem::path& logfile, config::LogLevel level);

/**
 * Shutdown the logging system
 */
void shutdown_logging();

/**
 * Set log level dynamically
 */
void set_log_level(config::LogLevel level);

/**
 * Convert our log level to Boost log severity
 */
inline boost::log::trivial::severity_level to_boost_severity(config::LogLevel level) {
    switch (level) {
        case config::LogLevel::DEBUG:
            return boost::log::trivial::debug;
        case config::LogLevel::INFO:
            return boost::log::trivial::info;
        case config::LogLevel::WARN:
            return boost::log::trivial::warning;
        case config::LogLevel::ERROR:
            return boost::log::trivial::error;
        default:
            return boost::log::trivial::info;
    }
}

} // namespace util
} // namespace supervisord

// Convenient logging macros
#define LOG_DEBUG   BOOST_LOG_TRIVIAL(debug)
#define LOG_INFO    BOOST_LOG_TRIVIAL(info)
#define LOG_WARN    BOOST_LOG_TRIVIAL(warning)
#define LOG_ERROR   BOOST_LOG_TRIVIAL(error)
