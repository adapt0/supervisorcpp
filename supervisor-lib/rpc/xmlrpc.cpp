#include "xmlrpc.h"
#include "../process/process.h"
#include "../util/string.h"
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
std::string Value::toString() const {
    return (std::ostringstream{} << *this).str();
}

std::ostream& operator<<(std::ostream& outs, const Member& member) {
    return outs << "<member><name>" << util::escape_xml(member.name_) << "</name><value>" << member.value_ << "</value></member>";
}
std::string Member::toString() const {
    return (std::ostringstream{} << *this).str();
}

std::ostream& operator<<(std::ostream& outs, const XmlProcessInfo& info) {
    return outs << "<struct>"
        << xmlrpc::Member("name", info.info_.name)
        << xmlrpc::Member("group", info.info_.name) // use name, as group not supported
        << xmlrpc::Member("statename", boost::lexical_cast<std::string>(info.info_.state))
        << xmlrpc::Member("state", static_cast<int>(info.info_.state))
        << xmlrpc::Member("pid", info.info_.pid)
        << xmlrpc::Member("exitstatus", info.info_.exitstatus)
        << xmlrpc::Member("stdout_logfile", info.info_.stdout_logfile)
        << xmlrpc::Member("spawnerr", info.info_.spawnerr)
        << xmlrpc::Member("description", info.info_.description)
        << "</struct>"
    ;
}

} // supervisorcpp::xmlrpc
