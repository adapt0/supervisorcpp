#include "config_parser.h"
#include "ptree_ext.h"
#include "../util/secure.h"
#include "../util/string.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace supervisorcpp::config {

constexpr const auto MAX_DEPTH = 10;

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

// Strip supervisord-style inline comments (; preceded by whitespace)
static std::string strip_inline_comment(const std::string& value) {
    for (size_t i = 1; i < value.size(); ++i) {
        if (value[i] == ';' && (value[i - 1] == ' ' || value[i - 1] == '\t')) {
            auto end = value.find_last_not_of(" \t", i - 1);
            return (end != std::string::npos) ? value.substr(0, end + 1) : "";
        }
    }
    return value;
}

static void strip_ptree_comments(pt::ptree& tree) {
    for (auto& [key, subtree] : tree) {
        const auto value = subtree.get_value<std::string>("");
        if (!value.empty()) {
            subtree.put_value(strip_inline_comment(value));
        }
        strip_ptree_comments(subtree);
    }
}


Configuration ConfigParser::parse_file(const fs::path& config_file) {
    if (!fs::exists(config_file)) {
        throw ConfigParseError("Configuration file not found: " + config_file.string());
    }

    Configuration config;
    parse_single_file_(config_file, config, 0);
    validate_config_(config);

    return config;
}

void ConfigParser::parse_single_file_(const std::filesystem::path& config_file, Configuration& config, size_t depth) {
    const auto inserted = config.included.insert(
        fs::weakly_canonical(config_file)
    ).second;
    if (!inserted) throw ConfigParseError("Configuration file " + config_file.string() + " has already been included!");

    // SECURITY: Validate config file ownership and permissions
    try {
        util::validate_config_file_security(config_file);
    } catch (const util::SecurityError& e) {
        throw ConfigParseError("Security validation failed: " + std::string(e.what()));
    }

    try {
        std::ifstream ifs{config_file.string()};
        parse_stream_(ifs, config, config_file.parent_path(), depth);
    } catch (const std::exception& e) {
        throw ConfigParseError("INI parsing error in " + config_file.string() + ": " + e.what());
    }
}

Configuration ConfigParser::parse_string(const std::string& config_str) {
    Configuration config;
    std::istringstream iss(config_str);
    try {
        parse_stream_(iss, config, std::filesystem::path{}, 0);
    } catch (const pt::ini_parser_error& e) {
        throw ConfigParseError("INI parsing error: " + std::string(e.what()));
    } catch (const ConfigParseError&) {
        throw; // Re-throw our own exceptions
    } catch (const std::exception& e) {
        throw ConfigParseError("Unexpected error: " + std::string(e.what()));
    }

    return config;
}

void ConfigParser::parse_stream_(std::istream& is, Configuration& config, const std::filesystem::path& base_dir, size_t depth) {
    if (!is) throw ConfigParseError("Invalid stream");
    if (depth >= MAX_DEPTH) throw ConfigParseError("Include depth limit of " + std::to_string(MAX_DEPTH) + " hit");

    pt::ptree tree;
    pt::read_ini(is, tree);
    strip_ptree_comments(tree);

    // Parse unix_http_server section
    if (const auto section = tree.get_child_optional("unix_http_server")) {
        if (auto file = section->get_optional<std::string>("file")) {
            config.unix_http_server.socket_file = *file;
        }
    }

    // Parse supervisord section
    if (const auto section = tree.get_child_optional("supervisord")) {
        pt_get(section, "childlogdir", config.supervisord.childlogdir);
        pt_get(section, "logfile",     config.supervisord.logfile);
        pt_get(section, "loglevel",    config.supervisord.loglevel, [](const auto& str) {
            try {
                return logger::parse_log_level(str);
            } catch (const std::invalid_argument& e) {
                throw ConfigParseError("[supervisord] loglevel - " + std::string(e.what()));
            }
        });
        pt_get(section, "user", config.supervisord.user);
    }

    // Parse supervisorctl section
    if (const auto section = tree.get_child_optional("supervisorctl")) {
        pt_get(section, "serverurl", config.supervisorctl.serverurl);
    }

    // Parse include section and recursively load included files
    if (const auto section = tree.get_child_optional("include")) {
        std::vector<std::string> patterns;
        for (const auto& [key, value] : *section) {
            if (key == "files") {
                // Split space-separated glob patterns (supervisord convention)
                std::istringstream iss(value.get_value<std::string>());
                std::string pattern;
                while (iss >> pattern) {
                    patterns.push_back(pattern);
                }
            }
        }

        if (!patterns.empty()) {
            if (!base_dir.empty()) parse_includes_(base_dir, patterns, config, depth + 1);
        }
    }

    // Parse program sections
    for (const auto& [key, value] : tree) {
        if (key.starts_with("program:")) {
            ProgramConfig prog;
            prog.name = key.substr(8); // Remove "program:" prefix

            // Required: command
            if (!pt_get(value, "command", prog.command) || prog.command.empty()) {
                throw ConfigParseError("Program [" + key + "] missing required 'command'");
            }

            // Optional: environment
            pt_get(value, "environment", prog.environment, [](const auto& str) {
                const auto parsed_env = parse_environment_(str);
                // SECURITY: Sanitize environment variables
                return util::sanitize_environment(parsed_env);
            });

            // Optional fields
            pt_get(value, "directory",    prog.directory, [](const std::string& s) -> fs::path { return s; });
            pt_get(value, "autorestart",  prog.autorestart);
            pt_get(value, "user",         prog.user);
            pt_get(value, "redirect_stderr", prog.redirect_stderr);
            pt_get(value, "startsecs",    prog.startsecs);
            pt_get(value, "startretries", prog.startretries);
            pt_get(value, "stopwaitsecs", prog.stopwaitsecs);

            pt_get(value, "stdout_logfile", prog.stdout_logfile, [&prog](const std::string& s) -> fs::path {
                return prog.substitute_variables(s);
            });
            pt_get(value, "stdout_logfile_maxbytes", prog.stdout_logfile_maxbytes, [&key](const std::string& s) -> size_t {
                try { return parse_size(s); } catch (const std::invalid_argument& e) {
                    throw ConfigParseError("Program [" + key + "]: " + e.what());
                }
            });
            pt_get(value, "stopsignal", prog.stopsignal, [&key](const std::string& s) {
                try { util::validate_signal(s); } catch (const std::invalid_argument& e) {
                    throw ConfigParseError("Program [" + key + "]: " + e.what());
                }
                return s;
            });

            // Apply variable substitution to command
            prog.command = prog.substitute_variables(prog.command);

            // SECURITY: Validate command path is absolute and safe
            try {
                util::validate_command_path(prog.command);
            } catch (const util::SecurityError& e) {
                throw ConfigParseError("Program [" + key + "]: " + e.what());
            }

            // SECURITY: Validate log file paths if specified
            if (prog.stdout_logfile) {
                try {
                    // This will throw if path is unsafe
                    prog.stdout_logfile = util::validate_log_path(*prog.stdout_logfile);
                } catch (const util::SecurityError& e) {
                    throw ConfigParseError("Program [" + key + "] stdout_logfile: " + e.what());
                }
            }

            config.programs.push_back(std::move(prog));
        }
    }
}

void ConfigParser::parse_includes_(const fs::path& base_dir,
                                  const std::vector<std::string>& patterns,
                                  Configuration& config, size_t depth) {
    for (const auto& pattern : patterns) {
        const auto files = expand_glob_(base_dir, pattern);
        for (const auto& file : files) {
            parse_single_file_(file, config, depth);
        }
    }
}

std::vector<fs::path> ConfigParser::expand_glob_(const fs::path& base_dir,
                                                 const std::string& pattern) {
    // Handle absolute vs relative paths
    fs::path search_path;
    if (pattern[0] == '/') {
        search_path = pattern;
    } else {
        search_path = base_dir / pattern;
    }

    // Extract directory and filename pattern
    const auto dir = search_path.parent_path();

    // Convert glob pattern to regex, escaping special regex characters (except * and ?)
    const std::regex file_regex{util::glob_to_regex(
        search_path.filename().string()
    )};

    // Search directory
    std::vector<fs::path> results;
    if (fs::exists(dir) && fs::is_directory(dir)) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, file_regex)) {
                    results.push_back(entry.path());
                }
            }
        }
    }

    // Sort results for consistent ordering
    std::sort(std::begin(results), std::end(results));

    return results;
}

std::map<std::string, std::string> ConfigParser::parse_environment_(const std::string& env_str) {
    // Split by comma
    std::istringstream iss(env_str);
    std::string pair;

    std::map<std::string, std::string> env_map;
    while (std::getline(iss, pair, ',')) {
        // Trim whitespace
        pair.erase(0, pair.find_first_not_of(" \t"));
        pair.erase(pair.find_last_not_of(" \t") + 1);

        // Skip empty pairs
        if (pair.empty()) continue;

        // Split by '='
        const size_t eq_pos = pair.find('=');
        if (eq_pos == std::string::npos) {
            throw ConfigParseError("Invalid environment variable format (missing '='): " + pair);
        }

        auto key   = pair.substr(0, eq_pos);
        auto value = pair.substr(eq_pos + 1);

        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Strip surrounding quotes from value if present
        if (value.size() >= 2) {
            if ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\'')) {
                value = value.substr(1, value.size() - 2);
            }
        }

        if (key.empty()) throw ConfigParseError("Invalid environment variable format (empty key): " + pair);

        env_map[key] = value;
    }

    return env_map;
}

void ConfigParser::validate_config_(const Configuration& config) {
    // Check that we have at least the required sections
    // unix_http_server is required for RPC
    if (config.unix_http_server.socket_file.empty()) {
        throw ConfigParseError("Missing [unix_http_server] section or 'file' parameter");
    }

    // Check that supervisord section has a logfile
    if (config.supervisord.logfile.empty()) {
        throw ConfigParseError("Missing [supervisord] section or 'logfile' parameter");
    }

    // Check that supervisorctl section has a serverurl
    if (config.supervisorctl.serverurl.empty()) {
        throw ConfigParseError("Missing [supervisorctl] section or 'serverurl' parameter");
    }

    // Validate each program has required fields
    for (const auto& prog : config.programs) {
        if (prog.command.empty()) {
            throw ConfigParseError("Program '" + prog.name + "' missing required 'command'");
        }
    }
}

} // namespace supervisorcpp::config
