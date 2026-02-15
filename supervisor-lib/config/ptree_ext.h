#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__PTREE_EXT
#define SUPERVISOR_LIB__PROCESS__PTREE_EXT

#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string/case_conv.hpp>

namespace supervisorcpp::config {

namespace pt = boost::property_tree;

template <typename T>
bool pt_get(const pt::ptree& tree, const pt::ptree::path_type& name, T& value) {
    const auto opt = tree.get_optional<T>(name);
    if (!opt) return false;

    value = *opt;
    return true;
}
template <>
bool pt_get<bool>(const pt::ptree& tree, const pt::ptree::path_type& name, bool& value) {
    const auto opt = tree.get_optional<std::string>(name);
    if (!opt) return false;

    const auto val = boost::algorithm::to_lower_copy(*opt);
    value = (val == "true" || val == "yes" || val == "1");
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
