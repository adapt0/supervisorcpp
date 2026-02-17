#pragma once
#ifndef SUPERVISOR_LIB__RPC__XMLRPC
#define SUPERVISOR_LIB__RPC__XMLRPC

#include <sstream>
#include <string_view>
#include <variant>
#include <vector>

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

    Value(Value&&) = default;
    Value& operator=(Value&&) = default;

    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;

    std::string str() const;

private:
    std::variant<std::string_view, int, bool> value_;
};


class Member {
public:
    friend std::ostream& operator<<(std::ostream& outs, const Member& member);

    template <typename T>
    Member(std::string_view name, T value) : name_{name}, value_{std::move(value)} { }

    Member(Member&&) = default;
    Member& operator=(Member&&) = default;

    Member(const Member&) = delete;
    Member& operator=(const Member&) = delete;

    std::string str() const;

private:
    std::string_view name_;
    Value value_;
};

class Struct {
public:
    friend std::ostream& operator<<(std::ostream& outs, const Struct& s);

    template<typename... Args>
    requires (std::is_same_v<std::decay_t<Args>, Member> && ...)
    Struct(Args&&... args) {
        items_.reserve(sizeof...(args));
        (items_.emplace_back(std::move(args)), ...);
    }

    Struct(Struct&&) = default;
    Struct& operator=(Struct&&) = default;

    Struct(const Struct&) = delete;
    Struct& operator=(const Struct&) = delete;

    std::string str() const;

private:
    std::vector<Member> items_;
};


/// XML RPC to ostream boiler plate
template <typename T>
struct XmlFromT {
    explicit XmlFromT(const T& value_arg) : value{value_arg} { }

    XmlFromT(XmlFromT&&) = delete;
    XmlFromT& operator=(XmlFromT&&) = delete;
    XmlFromT(const XmlFromT&) = delete;
    XmlFromT& operator=(const XmlFromT&) = delete;

    std::string str() const {
        return (std::ostringstream{} << *this).str();
    }

    const T& value;
};

// Specific overloads defined in xmlrpc.cpp
std::ostream& operator<<(std::ostream& outs, const XmlFromT<process::ProcessInfo>& from);
std::ostream& operator<<(std::ostream& outs, const XmlFromT<std::vector<process::ProcessInfo>>& from);

template <typename T>
inline XmlFromT<T> wrap(const T& value) {
    return XmlFromT<T>{value};
}

} // supervisorcpp::xmlrpc

#endif // SUPERVISOR_LIB__RPC__XMLRPC
