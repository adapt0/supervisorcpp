// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "logger.h"
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>

namespace supervisorcpp::logger {

namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;
namespace logging = boost::log;
namespace sinks = boost::log::sinks;

using TrivialSeverity = boost::log::trivial::severity_level;

namespace {

/**
 * Convert our log level to Boost log severity
 */
static TrivialSeverity to_trivial_severity(LogLevel level) {
    switch (level) {
    case LogLevel::TRACE: return TrivialSeverity::trace;
    case LogLevel::DEBUG: return TrivialSeverity::debug;
    case LogLevel::INFO: return TrivialSeverity::info;
    case LogLevel::WARN: return TrivialSeverity::warning;
    case LogLevel::ERROR: return TrivialSeverity::error;
    case LogLevel::IGNORE: return TrivialSeverity::fatal;
    }
    return TrivialSeverity::info;
}

enum class LogDest {
    BOTH, // should be unused, lack of Dest implies both
    CONSOLE,
    FILE,
};

BOOST_LOG_ATTRIBUTE_KEYWORD(log_dest, "Dest", LogDest)

struct LogLevelSeverity {
    explicit LogLevelSeverity(LogLevel level_arg = LogLevel::INFO)
    : level{level_arg}
    , severity{to_trivial_severity(level_arg)}
    { }

    bool set(LogLevel level_arg) {
        if (level_arg == level) return false;
        level = level_arg;
        severity = to_trivial_severity(level_arg);
        return true;
    }

    LogLevel level;
    TrivialSeverity severity;
};

struct Logger {
    static auto& instance() {
        static Logger logger;
        return logger;
    }

    LogLevelSeverity console_sev{LogLevel::INFO};
    LogLevelSeverity file_sev{LogLevel::INFO};
};


} // anonymous namespace

void init_logging(LogLevel level) {
    // Add common attributes (timestamp, thread id, etc.)
    logging::add_common_attributes();

    // Console sink — filter out records tagged as file-only
    const auto log_console = logging::add_console_log(
        std::clog,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );

    // Set filter level
    auto& logger = Logger::instance();
    logger.console_sev.set(level);
    log_console->set_filter(
        (!expr::has_attr(log_dest) || log_dest != LogDest::FILE)
        && logging::trivial::severity >= boost::ref(logger.console_sev.severity)
    );
}

void init_file_logging(const std::filesystem::path& logfile,
                       LogLevel level,
                       size_t max_bytes, int backups,
                       std::string_view header) {
    if (logfile.empty()) return;

    // File sink with rotation and collection
    const auto log_file = logging::add_file_log(
        keywords::file_name = logfile.string(),
        keywords::open_mode = std::ios::app,
        keywords::rotation_size = max_bytes,
        keywords::target = logfile.parent_path().string(),
        keywords::max_files = static_cast<unsigned>(std::max(backups, 0)),
        keywords::auto_flush = true,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );

    // Set filter level
    auto& logger = Logger::instance();
    logger.file_sev.set(level);
    log_file->set_filter(
        (!expr::has_attr(log_dest) || log_dest != LogDest::CONSOLE)
        && logging::trivial::severity >= boost::ref(logger.file_sev.severity)
    );

    // Write delimiting header to file only
    if (!header.empty()) {
        BOOST_LOG_SCOPED_THREAD_ATTR("Dest", logging::attributes::constant(LogDest::FILE));
        LOG_INFO << "------------------------------------------------------------";
        LOG_INFO << header;
    }
}

void shutdown_logging() {
    LOG_TRACE << "Shutting down logging system";
    logging::core::get()->remove_all_sinks();
}

LogLevel get_log_level() {
    return Logger::instance().console_sev.level;
}

void set_log_level(LogLevel level) {
    if (Logger::instance().console_sev.set(level)) {
        LOG_TRACE << "Log level changed to: " << level;
    }
}

void increment_log_level(int amount) {
    const auto level = std::clamp(
        static_cast<LogLevel>(static_cast<int>(get_log_level()) - amount),
        LogLevel::TRACE,
        LogLevel::ERROR
    );
    set_log_level(level);
}

} // namespace supervisorcpp::logger
