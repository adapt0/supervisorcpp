// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "xmlrpc.h"
#include "process/process.h"
#include "util/string.h"
#include <sstream>
#include <boost/lexical_cast.hpp>

namespace supervisorcpp::xmlrpc {

std::ostream& operator<<(std::ostream& outs, const Value& value) {
    std::visit([&outs](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
            outs << "<string>" << util::escape_xml(arg) << "</string>";
        } else if constexpr (std::is_same_v<T, int>) {
            outs << "<int>" << arg << "</int>";
        } else if constexpr (std::is_same_v<T, bool>) {
            outs << "<boolean>" << arg << "</boolean>";
        } else {
            static_assert(false, "non-exhaustive visitor!");
        }
    }, value.value_);

    return outs;
}
std::string Value::str() const {
    return (std::ostringstream{} << *this).str();
}

std::ostream& operator<<(std::ostream& outs, const Member& member) {
    return outs << "<member>"
        << "<name>" << util::escape_xml(member.name_) << "</name>"
        << "<value>" << member.value_ << "</value>"
        << "</member>"
    ;
}
std::string Member::str() const {
    return (std::ostringstream{} << *this).str();
}

std::ostream& operator<<(std::ostream& outs, const Struct& s) {
    outs << "<struct>";
    for (const auto& item : s.items_) {
        outs << item;
    }
    return outs << "</struct>";
}
std::string Struct::str() const {
    return (std::ostringstream{} << *this).str();
}


std::ostream& operator<<(std::ostream& outs, const XmlFromT<process::ProcessInfo>& from) {
    return outs << Struct{
        Member{"name", from.value.name},
        Member{"group", from.value.name}, // use name, as group not supported
        Member("statename", boost::lexical_cast<std::string>(from.value.state)),
        Member("state", static_cast<int>(from.value.state)),
        Member("pid", from.value.pid),
        Member("exitstatus", from.value.exitstatus),
        Member("stdout_logfile", from.value.stdout_logfile),
        Member("spawnerr", from.value.spawnerr),
        Member("description", from.value.description),
    };
}

std::ostream& operator<<(std::ostream& outs, const XmlFromT<std::vector<process::ProcessInfo>>& from) {
    outs << "<array><data>";
    for (const auto& info : from.value) {
        outs << "<value>" << wrap(info) << "</value>";
    }
    return outs << "</data></array>";
}

} // supervisorcpp::xmlrpc
