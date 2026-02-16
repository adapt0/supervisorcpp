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
    }
    return TrivialSeverity::info;
}

BOOST_LOG_ATTRIBUTE_KEYWORD(file_only, "FileOnly", bool)

static LogLevel current_level = LogLevel::INFO;
static TrivialSeverity current_trivial_severity = to_trivial_severity(current_level);

} // anonymous namespace

void init_logging(LogLevel level) {
    // Add common attributes (timestamp, thread id, etc.)
    logging::add_common_attributes();

    // Console sink — filter out records tagged as file-only
    auto console = logging::add_console_log(
        std::clog,
        keywords::format = "[%TimeStamp%] [%Severity%] %Message%"
    );

    // Set filter level
    current_level = level;
    current_trivial_severity = to_trivial_severity(level);
    logging::core::get()->set_filter(
        !expr::has_attr(file_only)
        && logging::trivial::severity >= boost::ref(current_trivial_severity)
    );
}

void init_file_logging(const std::filesystem::path& logfile,
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
    log_file->set_filter(
        logging::trivial::severity >= TrivialSeverity::info
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

LogLevel get_log_level() {
    return current_level;
}

void set_log_level(LogLevel level) {
    current_level = level;
    current_trivial_severity = to_trivial_severity(level);
    LOG_TRACE << "Log level changed to: " << level;
}

void increment_log_level(int amount) {
    const auto level = std::clamp(
        static_cast<LogLevel>(static_cast<int>(current_level) - amount),
        LogLevel::TRACE,
        LogLevel::ERROR
    );
    if (level != current_level) set_log_level(level);
}

} // namespace supervisorcpp::logger
