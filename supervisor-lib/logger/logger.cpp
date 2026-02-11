#include "logger.h"
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/support/date_time.hpp>

namespace supervisorcpp::logger {

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;

namespace {

/**
 * Convert our log level to Boost log severity
 */
boost::log::trivial::severity_level to_boost_severity(LogLevel level) {
    switch (level) {
    case LogLevel::TRACE: return boost::log::trivial::trace;
    case LogLevel::DEBUG: return boost::log::trivial::debug;
    case LogLevel::INFO: return boost::log::trivial::info;
    case LogLevel::WARN: return boost::log::trivial::warning;
    case LogLevel::ERROR: return boost::log::trivial::error;
    }
    return boost::log::trivial::info;
}

} // anonymous namespace

void init_logging(const std::filesystem::path& logfile, LogLevel level) {
    // Add common attributes (timestamp, thread id, etc.)
    logging::add_common_attributes();

    // Console sink (for debugging)
    auto console_sink = logging::add_console_log(
        std::clog,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );

    // File sink
    auto file_sink = logging::add_file_log(
        keywords::file_name = logfile.string(),
        keywords::rotation_size = 10 * 1024 * 1024, // 10 MB
        keywords::auto_flush = true,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );

    // Set filter level
    logging::core::get()->set_filter(
        logging::trivial::severity >= to_boost_severity(level)
    );

    LOG_INFO << "Logging initialized: " << logfile.string() << " (level: " << level << ")";
}

void shutdown_logging() {
    LOG_INFO << "Shutting down logging system";
    logging::core::get()->remove_all_sinks();
}

void set_log_level(LogLevel level) {
    logging::core::get()->set_filter(
        logging::trivial::severity >= to_boost_severity(level)
    );
    LOG_INFO << "Log level changed to: " << level;
}

} // namespace supervisorcpp::logger
