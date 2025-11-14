#include "logger.h"
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/support/date_time.hpp>
#include <iostream>

namespace supervisord {
namespace util {

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;

void init_logging(const std::filesystem::path& logfile, config::LogLevel level) {
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

    LOG_INFO << "Logging initialized: " << logfile.string()
             << " (level: " << config::log_level_to_string(level) << ")";
}

void shutdown_logging() {
    LOG_INFO << "Shutting down logging system";
    logging::core::get()->remove_all_sinks();
}

void set_log_level(config::LogLevel level) {
    logging::core::get()->set_filter(
        logging::trivial::severity >= to_boost_severity(level)
    );
    LOG_INFO << "Log level changed to: " << config::log_level_to_string(level);
}

} // namespace util
} // namespace supervisord
