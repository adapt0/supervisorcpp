// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "logger.h"
#include <boost/algorithm/string/case_conv.hpp>

namespace supervisorcpp::logger {

std::ostream& operator<<(std::ostream& os, LogLevel level) {
    switch (level) {
    case LogLevel::TRACE: return os << "trace";
    case LogLevel::DEBUG: return os << "debug";
    case LogLevel::INFO: return os << "info";
    case LogLevel::WARN: return os << "warn";
    case LogLevel::ERROR: return os << "error";
    case LogLevel::IGNORE: return os << "ignore";
    }
    return os << "unknown[" << static_cast<int>(level) << ']';
}

LogLevel parse_log_level(std::string level_str) {
    boost::algorithm::to_lower(level_str);
    if (level_str == "trace") return LogLevel::TRACE;
    if (level_str == "debug") return LogLevel::DEBUG;
    if (level_str == "info") return LogLevel::INFO;
    if (level_str == "warn" || level_str == "warning") return LogLevel::WARN;
    if (level_str == "error") return LogLevel::ERROR;
    if (level_str == "ignore") return LogLevel::IGNORE;
    throw std::invalid_argument("Invalid log level: " + level_str);
}

} // namespace supervisorcpp::logger
