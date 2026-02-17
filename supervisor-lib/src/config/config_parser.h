// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__CONFIG__CONFIG_PARSER
#define SUPERVISOR_LIB__CONFIG__CONFIG_PARSER

#include "config_types.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace supervisorcpp::config {

/**
 * Exception thrown when configuration parsing fails
 */
class ConfigParseError : public std::runtime_error {
public:
    explicit ConfigParseError(const std::string& msg)
        : std::runtime_error(msg) {}
};

/**
 * Wrap a parser function to rethrow std::invalid_argument as ConfigParseError
 * Usage: pt_get(section, "key", value, parse_config(parse_size, "[supervisord] key"))
 */
template <typename Func>
requires std::invocable<Func, const std::string&>
auto parse_config(Func&& parser, std::string context) {
    return [
        parser = std::forward<Func>(parser),
        context = std::move(context)
    ](const std::string& s) {
        try {
            if constexpr (std::is_void<decltype(parser(s))>::value) {
                parser(s);
                return s;
            } else {
                return parser(s);
            }
        } catch (const std::invalid_argument& e) {
            throw ConfigParseError(context + ": " + e.what());
        }
    };
}

/**
 * Configuration parser for supervisord INI files
 */
class ConfigParser {
public:
    /**
     * Parse configuration from a file
     * @param config_file Path to the main configuration file
     * @return Parsed configuration
     * @throws ConfigParseError if parsing fails
     */
    static Configuration parse_file(const std::filesystem::path& config_file);

    /**
     * Parse configuration from a string
     * @param config_str Configuration content as string
     * @return Parsed configuration
     * @throws ConfigParseError if parsing fails
     */
    static Configuration parse_string(const std::string& config_str);

private:
    /**
     * Parse a single INI file into the configuration
     * @param config_file Path to INI file
     * @param config Configuration to populate
     */
    static void parse_single_file_(const std::filesystem::path& config_file, Configuration& config, size_t depth);

    /**
     * Parse a INI from a stream
     * @param is read stream
     * @param config Configuration to populate
     */
    static void parse_stream_(std::istream& is, Configuration& config, const std::filesystem::path& base_dir, size_t depth);

    /**
     * Parse included files (from [include] section)
     * @param base_dir Base directory for relative paths
     * @param patterns Glob patterns to match
     * @param config Configuration to populate
     * @param depth Current include depth + 1
     */
    static void parse_includes_(const std::filesystem::path& base_dir,
                               const std::vector<std::string>& patterns,
                               Configuration& config,
                               size_t depth);

    /**
     * Expand glob pattern to list of files
     * @param base_dir Base directory
     * @param pattern Glob pattern (e.g., "*.ini")
     * @return Vector of matching file paths
     */
    static std::vector<std::filesystem::path> expand_glob_(
        const std::filesystem::path& base_dir,
        const std::string& pattern);

    /**
     * Parse environment string (KEY=value,KEY2=value2) into map
     * @param env_str Environment string
     * @return Map of environment variables
     */
    static std::map<std::string, std::string> parse_environment_(
        const std::string& env_str);

    /**
     * Validate configuration for required fields
     * @param config Configuration to validate
     * @throws ConfigParseError if validation fails
     */
    static void validate_config_(const Configuration& config);
};

} // namespace supervisorcpp::config

#endif // SUPERVISOR_LIB__CONFIG__CONFIG_PARSER
