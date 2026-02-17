#pragma once
#ifndef SUPERVISOR_LIB__UTIL__STRING
#define SUPERVISOR_LIB__UTIL__STRING

#include <string>
#include <unordered_map>

namespace supervisorcpp::util {

using Needles = std::unordered_map<char, std::string>;

/**
 * escape a string, substituting needles
 */
std::string escape_str(const std::string_view& str, const Needles& needles);

std::string escape_xml(const std::string_view& str);
std::string glob_to_regex(const std::string_view& str);

} // namespace supervisorcpp::util

#endif // SUPERVISOR_LIB__UTIL__STRING
