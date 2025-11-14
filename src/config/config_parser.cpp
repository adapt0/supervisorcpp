#include "config_parser.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <sstream>
#include <algorithm>
#include <regex>

namespace supervisord {
namespace config {

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

Configuration ConfigParser::parse_file(const fs::path& config_file) {
    if (!fs::exists(config_file)) {
        throw ConfigParseError("Configuration file not found: " + config_file.string());
    }

    Configuration config;
    parse_single_file(config_file, config);

    // Validate the configuration
    validate_config(config);

    return config;
}

Configuration ConfigParser::parse_string(const std::string& config_str) {
    Configuration config;

    try {
        pt::ptree tree;
        std::istringstream iss(config_str);
        pt::read_ini(iss, tree);

        // Parse unix_http_server section
        if (auto section = tree.get_child_optional("unix_http_server")) {
            if (auto file = section->get_optional<std::string>("file")) {
                config.unix_http_server.socket_file = *file;
            }
        }

        // Parse supervisord section
        if (auto section = tree.get_child_optional("supervisord")) {
            if (auto logfile = section->get_optional<std::string>("logfile")) {
                config.supervisord.logfile = *logfile;
            }
            if (auto loglevel = section->get_optional<std::string>("loglevel")) {
                config.supervisord.loglevel = parse_log_level(*loglevel);
            }
            if (auto user = section->get_optional<std::string>("user")) {
                config.supervisord.user = *user;
            }
            if (auto childlogdir = section->get_optional<std::string>("childlogdir")) {
                config.supervisord.childlogdir = *childlogdir;
            }
        }

        // Parse supervisorctl section
        if (auto section = tree.get_child_optional("supervisorctl")) {
            if (auto serverurl = section->get_optional<std::string>("serverurl")) {
                config.supervisorctl.serverurl = *serverurl;
            }
        }

        // Parse program sections
        for (const auto& [key, value] : tree) {
            if (key.starts_with("program:")) {
                ProgramConfig prog;
                prog.name = key.substr(8); // Remove "program:" prefix

                // Required: command
                if (auto command = value.get_optional<std::string>("command")) {
                    prog.command = *command;
                } else {
                    throw ConfigParseError("Program [" + key + "] missing required 'command'");
                }

                // Optional: environment
                if (auto env = value.get_optional<std::string>("environment")) {
                    prog.environment = parse_environment(*env);
                }

                // Optional: directory
                if (auto dir = value.get_optional<std::string>("directory")) {
                    prog.directory = *dir;
                }

                // Optional: autorestart
                if (auto autorestart = value.get_optional<std::string>("autorestart")) {
                    std::string val = *autorestart;
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    prog.autorestart = (val == "true" || val == "yes" || val == "1");
                }

                // Optional: user
                if (auto user = value.get_optional<std::string>("user")) {
                    prog.user = *user;
                }

                // Optional: stdout_logfile
                if (auto logfile = value.get_optional<std::string>("stdout_logfile")) {
                    prog.stdout_logfile = prog.substitute_variables(*logfile);
                }

                // Optional: stdout_logfile_maxbytes
                if (auto maxbytes = value.get_optional<std::string>("stdout_logfile_maxbytes")) {
                    prog.stdout_logfile_maxbytes = parse_size(*maxbytes);
                }

                // Optional: redirect_stderr
                if (auto redirect = value.get_optional<std::string>("redirect_stderr")) {
                    std::string val = *redirect;
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    prog.redirect_stderr = (val == "true" || val == "yes" || val == "1");
                }

                // Optional: startsecs
                if (auto startsecs = value.get_optional<int>("startsecs")) {
                    prog.startsecs = *startsecs;
                }

                // Optional: startretries
                if (auto startretries = value.get_optional<int>("startretries")) {
                    prog.startretries = *startretries;
                }

                // Optional: stopwaitsecs
                if (auto stopwaitsecs = value.get_optional<int>("stopwaitsecs")) {
                    prog.stopwaitsecs = *stopwaitsecs;
                }

                // Optional: stopsignal
                if (auto stopsignal = value.get_optional<std::string>("stopsignal")) {
                    prog.stopsignal = *stopsignal;
                }

                config.programs.push_back(std::move(prog));
            }
        }

    } catch (const pt::ini_parser_error& e) {
        throw ConfigParseError("INI parsing error: " + std::string(e.what()));
    } catch (const ConfigParseError&) {
        throw; // Re-throw our own exceptions
    } catch (const std::exception& e) {
        throw ConfigParseError("Unexpected error: " + std::string(e.what()));
    }

    return config;
}

void ConfigParser::parse_single_file(const fs::path& config_file, Configuration& config) {
    try {
        pt::ptree tree;
        pt::read_ini(config_file.string(), tree);

        // Parse unix_http_server section
        if (auto section = tree.get_child_optional("unix_http_server")) {
            if (auto file = section->get_optional<std::string>("file")) {
                config.unix_http_server.socket_file = *file;
            }
        }

        // Parse supervisord section
        if (auto section = tree.get_child_optional("supervisord")) {
            if (auto logfile = section->get_optional<std::string>("logfile")) {
                config.supervisord.logfile = *logfile;
            }
            if (auto loglevel = section->get_optional<std::string>("loglevel")) {
                config.supervisord.loglevel = parse_log_level(*loglevel);
            }
            if (auto user = section->get_optional<std::string>("user")) {
                config.supervisord.user = *user;
            }
            if (auto childlogdir = section->get_optional<std::string>("childlogdir")) {
                config.supervisord.childlogdir = *childlogdir;
            }
        }

        // Parse supervisorctl section
        if (auto section = tree.get_child_optional("supervisorctl")) {
            if (auto serverurl = section->get_optional<std::string>("serverurl")) {
                config.supervisorctl.serverurl = *serverurl;
            }
        }

        // Parse include section and recursively load included files
        if (auto section = tree.get_child_optional("include")) {
            std::vector<std::string> patterns;
            for (const auto& [key, value] : *section) {
                if (key == "files") {
                    patterns.push_back(value.get_value<std::string>());
                }
            }

            if (!patterns.empty()) {
                fs::path base_dir = config_file.parent_path();
                if (base_dir.empty()) {
                    base_dir = fs::current_path();
                }
                parse_includes(base_dir, patterns, config);
            }
        }

        // Parse program sections
        for (const auto& [key, value] : tree) {
            if (key.starts_with("program:")) {
                ProgramConfig prog;
                prog.name = key.substr(8); // Remove "program:" prefix

                // Required: command
                if (auto command = value.get_optional<std::string>("command")) {
                    prog.command = *command;
                } else {
                    throw ConfigParseError("Program [" + key + "] missing required 'command'");
                }

                // Optional: environment
                if (auto env = value.get_optional<std::string>("environment")) {
                    prog.environment = parse_environment(*env);
                }

                // Optional: directory
                if (auto dir = value.get_optional<std::string>("directory")) {
                    prog.directory = *dir;
                }

                // Optional: autorestart
                if (auto autorestart = value.get_optional<std::string>("autorestart")) {
                    std::string val = *autorestart;
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    prog.autorestart = (val == "true" || val == "yes" || val == "1");
                }

                // Optional: user
                if (auto user = value.get_optional<std::string>("user")) {
                    prog.user = *user;
                }

                // Optional: stdout_logfile
                if (auto logfile = value.get_optional<std::string>("stdout_logfile")) {
                    prog.stdout_logfile = prog.substitute_variables(*logfile);
                }

                // Optional: stdout_logfile_maxbytes
                if (auto maxbytes = value.get_optional<std::string>("stdout_logfile_maxbytes")) {
                    prog.stdout_logfile_maxbytes = parse_size(*maxbytes);
                }

                // Optional: redirect_stderr
                if (auto redirect = value.get_optional<std::string>("redirect_stderr")) {
                    std::string val = *redirect;
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    prog.redirect_stderr = (val == "true" || val == "yes" || val == "1");
                }

                // Optional: startsecs
                if (auto startsecs = value.get_optional<int>("startsecs")) {
                    prog.startsecs = *startsecs;
                }

                // Optional: startretries
                if (auto startretries = value.get_optional<int>("startretries")) {
                    prog.startretries = *startretries;
                }

                // Optional: stopwaitsecs
                if (auto stopwaitsecs = value.get_optional<int>("stopwaitsecs")) {
                    prog.stopwaitsecs = *stopwaitsecs;
                }

                // Optional: stopsignal
                if (auto stopsignal = value.get_optional<std::string>("stopsignal")) {
                    prog.stopsignal = *stopsignal;
                }

                config.programs.push_back(std::move(prog));
            }
        }

    } catch (const pt::ini_parser_error& e) {
        throw ConfigParseError("INI parsing error in " + config_file.string() + ": " + e.what());
    }
}

void ConfigParser::parse_includes(const fs::path& base_dir,
                                  const std::vector<std::string>& patterns,
                                  Configuration& config) {
    for (const auto& pattern : patterns) {
        auto files = expand_glob(base_dir, pattern);
        for (const auto& file : files) {
            parse_single_file(file, config);
        }
    }
}

std::vector<fs::path> ConfigParser::expand_glob(const fs::path& base_dir,
                                                 const std::string& pattern) {
    std::vector<fs::path> results;

    // Handle absolute vs relative paths
    fs::path search_path;
    if (pattern[0] == '/') {
        search_path = pattern;
    } else {
        search_path = base_dir / pattern;
    }

    // Extract directory and filename pattern
    fs::path dir = search_path.parent_path();
    std::string filename_pattern = search_path.filename().string();

    // Convert glob pattern to regex
    std::string regex_pattern = filename_pattern;
    // Escape special regex characters except * and ?
    regex_pattern = std::regex_replace(regex_pattern, std::regex("\\."), "\\.");
    regex_pattern = std::regex_replace(regex_pattern, std::regex("\\*"), ".*");
    regex_pattern = std::regex_replace(regex_pattern, std::regex("\\?"), ".");

    std::regex file_regex(regex_pattern);

    // Search directory
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
    std::sort(results.begin(), results.end());

    return results;
}

std::map<std::string, std::string> ConfigParser::parse_environment(const std::string& env_str) {
    std::map<std::string, std::string> env_map;

    // Split by comma
    std::istringstream iss(env_str);
    std::string pair;

    while (std::getline(iss, pair, ',')) {
        // Trim whitespace
        pair.erase(0, pair.find_first_not_of(" \t"));
        pair.erase(pair.find_last_not_of(" \t") + 1);

        // Split by '='
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);

            // Trim key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            env_map[key] = value;
        }
    }

    return env_map;
}

void ConfigParser::validate_config(const Configuration& config) {
    // Check that we have at least the required sections
    // unix_http_server is required for RPC
    if (config.unix_http_server.socket_file.empty()) {
        throw ConfigParseError("Missing [unix_http_server] section or 'file' parameter");
    }

    // Validate each program has required fields
    for (const auto& prog : config.programs) {
        if (prog.command.empty()) {
            throw ConfigParseError("Program '" + prog.name + "' missing required 'command'");
        }
    }
}

} // namespace config
} // namespace supervisord
