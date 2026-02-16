#include "logger.h"
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
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

BOOST_LOG_ATTRIBUTE_KEYWORD(file_only, "FileOnly", bool)

} // anonymous namespace

void init_logging() {
    // Add common attributes (timestamp, thread id, etc.)
    logging::add_common_attributes();

    // Console sink — filter out records tagged as file-only
    auto console = logging::add_console_log(
        std::clog,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );
    console->set_filter(!expr::has_attr(file_only));
}

void init_file_logging(const std::filesystem::path& logfile, LogLevel level,
                       size_t max_bytes, int backups,
                       std::string_view header) {
    if (logfile.empty()) return;

    // File sink with rotation and collection
    logging::add_file_log(
        keywords::file_name = logfile.string(),
        keywords::open_mode = std::ios::app,
        keywords::rotation_size = max_bytes,
        keywords::target = logfile.parent_path().string(),
        keywords::max_files = static_cast<unsigned>(std::max(backups, 0)),
        keywords::auto_flush = true,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );

    // Set filter level
    logging::core::get()->set_filter(
        logging::trivial::severity >= to_boost_severity(level)
    );

    // Write delimiting header to file only
    if (!header.empty()) {
        BOOST_LOG_SCOPED_THREAD_ATTR("FileOnly", logging::attributes::constant<bool>(true));
        LOG_INFO << "------------------------------------------------------------";
        LOG_INFO << header;
    }
}

void shutdown_logging() {
    LOG_TRACE << "Shutting down logging system";
    logging::core::get()->remove_all_sinks();
}

void set_log_level(LogLevel level) {
    logging::core::get()->set_filter(
        logging::trivial::severity >= to_boost_severity(level)
    );
    LOG_INFO << "Log level changed to: " << level;
}

} // namespace supervisorcpp::logger
