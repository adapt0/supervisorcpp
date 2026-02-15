#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__PTREE_EXT
#define SUPERVISOR_LIB__PROCESS__PTREE_EXT

#include <charconv>
#include <stdexcept>
#include <type_traits>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string/case_conv.hpp>

namespace supervisorcpp::config {

namespace pt = boost::property_tree;

template <typename T>
bool pt_get(const pt::ptree& tree, const pt::ptree::path_type& name, T& value) {
    const auto str_opt = tree.get_optional<std::string>(name);
    if (!str_opt) return false;

    if constexpr (std::is_same_v<T, bool>) {
        const auto val = boost::algorithm::to_lower_copy(*str_opt);
        if (val == "true" || val == "yes" || val == "1") {
            value = true;
        } else if (val == "false" || val == "no" || val == "0") {
            value = false;
        } else {
            throw std::invalid_argument(
                "Invalid value for '" + name.dump() + "': " + *str_opt
            );
        }
    } else if constexpr (std::is_arithmetic_v<T>) {
        T result{};
        const auto& s = *str_opt;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
        if (ec != std::errc{} || ptr != s.data() + s.size()) {
            throw std::invalid_argument(
                "Invalid value for '" + name.dump() + "': " + s
            );
        }
        value = result;
    } else if constexpr (std::is_constructible_v<T, const std::string&>) {
        value = T{*str_opt};
    } else {
        static_assert(!sizeof(T), "Unsupported type for pt_get");
    }

    return true;
}

template <typename T = std::string, typename V, typename FUNC>
requires std::invocable<FUNC, T>
bool pt_get(const pt::ptree& tree, const pt::ptree::path_type& name, V& value, FUNC&& func) {
    const auto opt = tree.get_optional<T>(name);
    if (!opt) return false;
    value = func(*opt);
    return true;
}

template <typename... ARGS>
bool pt_get(const boost::optional<pt::ptree&>& tree_opt, const pt::ptree::path_type& name, ARGS&&... args) {
    return (tree_opt) ? pt_get(*tree_opt, name, std::forward<ARGS>(args)...) : false;
}

} // namespace supervisorcpp::config

#endif // SUPERVISOR_LIB__PROCESS__PTREE_EXT
