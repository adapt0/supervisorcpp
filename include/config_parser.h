#pragma once

#include "config_types.h"
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

namespace supervisord {
namespace config {

/**
 * Exception thrown when configuration parsing fails
 */
class ConfigParseError : public std::runtime_error {
public:
    explicit ConfigParseError(const std::string& msg)
        : std::runtime_error(msg) {}
};

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
    static void parse_single_file(const std::filesystem::path& config_file,
                                   Configuration& config);

    /**
     * Parse included files (from [include] section)
     * @param base_dir Base directory for relative paths
     * @param patterns Glob patterns to match
     * @param config Configuration to populate
     */
    static void parse_includes(const std::filesystem::path& base_dir,
                               const std::vector<std::string>& patterns,
                               Configuration& config);

    /**
     * Expand glob pattern to list of files
     * @param base_dir Base directory
     * @param pattern Glob pattern (e.g., "*.ini")
     * @return Vector of matching file paths
     */
    static std::vector<std::filesystem::path> expand_glob(
        const std::filesystem::path& base_dir,
        const std::string& pattern);

    /**
     * Parse environment string (KEY=value,KEY2=value2) into map
     * @param env_str Environment string
     * @return Map of environment variables
     */
    static std::map<std::string, std::string> parse_environment(
        const std::string& env_str);

    /**
     * Validate configuration for required fields
     * @param config Configuration to validate
     * @throws ConfigParseError if validation fails
     */
    static void validate_config(const Configuration& config);
};

} // namespace config
} // namespace supervisord
