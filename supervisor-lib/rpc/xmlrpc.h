#pragma once
#ifndef SUPERVISOR_LIB__RPC__XMLRPC
#define SUPERVISOR_LIB__RPC__XMLRPC

#include <iosfwd>
#include <string_view>
#include <variant>

namespace supervisorcpp::process {
    struct ProcessInfo;
}

namespace supervisorcpp::xmlrpc {

class Value {
public:
    friend std::ostream& operator<<(std::ostream& outs, const Value& value);

    Value(std::string_view str) : value_{str} { }

    template <typename T>
    requires std::is_integral_v<T>
    Value(T i) : value_{i} { }

    Value(Value&&) = delete;
    Value& operator=(Value&&) = delete;
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;

    std::string toString() const;

private:
    std::variant<std::string_view, int, bool> value_;
};


class Member {
public:
    friend std::ostream& operator<<(std::ostream& outs, const Member& member);

    template <typename T>
    Member(std::string_view name, T value) : name_{name}, value_{std::move(value)} { }

    Member(Member&&) = delete;
    Member& operator=(Member&&) = delete;
    Member(const Member&) = delete;
    Member& operator=(const Member&) = delete;

    std::string toString() const;

private:
    std::string_view name_;
    Value value_;
};


/// xmlrpc stream helper for process::ProcessInfo
struct XmlProcessInfo {
    friend std::ostream& operator<<(std::ostream& outs, const XmlProcessInfo& info);

    XmlProcessInfo(const process::ProcessInfo& info) : info_{info} { }

    XmlProcessInfo(XmlProcessInfo&&) = delete;
    XmlProcessInfo& operator=(XmlProcessInfo&&) = delete;
    XmlProcessInfo(const XmlProcessInfo&) = delete;
    XmlProcessInfo& operator=(const XmlProcessInfo&) = delete;

private:
    const process::ProcessInfo& info_;
};

} // supervisorcpp::xmlrpc

#endif // SUPERVISOR_LIB__RPC__XMLRPC
