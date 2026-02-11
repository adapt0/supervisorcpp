#pragma once
#ifndef SUPERVISOR_LIB__CONFIG__VALIDATION
#define SUPERVISOR_LIB__CONFIG__VALIDATION

#include <filesystem>
#include <map>

namespace supervisord::config {

void validate_config_file_security(const std::filesystem::path& config_file);
std::filesystem::path validate_log_path(const std::filesystem::path& log_path);
void validate_command_path(const std::string& command);

std::map<std::string, std::string> sanitize_environment(const std::map<std::string, std::string>& env);

} // namespace supervisord::config

#endif // SUPERVISOR_LIB__CONFIG__VALIDATION
