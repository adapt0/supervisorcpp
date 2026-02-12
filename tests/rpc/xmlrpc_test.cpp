#define BOOST_TEST_MODULE XmlRpcTest
#include <boost/test/unit_test.hpp>
#include "process/process.h"
#include "rpc/xmlrpc.h"
#include <sstream>

namespace process = supervisorcpp::process;
namespace xmlrpc = supervisorcpp::xmlrpc;

BOOST_AUTO_TEST_CASE(xmlrpc__value) {
    BOOST_CHECK_EQUAL(xmlrpc::Value{"Test"}.toString(), "<string>Test</string>");

    BOOST_CHECK_EQUAL(xmlrpc::Value{42}.toString(), "<int>42</int>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{-2}.toString(), "<int>-2</int>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{0}.toString(), "<int>0</int>");

    BOOST_CHECK_EQUAL(xmlrpc::Value{true}.toString(), "<boolean>1</boolean>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{false}.toString(), "<boolean>0</boolean>");

    BOOST_CHECK_EQUAL(xmlrpc::Member("ABoolean", true).toString(), "<member><name>ABoolean</name><value><boolean>1</boolean></value></member>");
    BOOST_CHECK_EQUAL(xmlrpc::Member("Beg<>End", "Beg & End").toString(), "<member><name>Beg&lt;&gt;End</name><value><string>Beg &amp; End</string></value></member>");
}

BOOST_AUTO_TEST_CASE(xmlrpc__process_state_strings) {
    auto state_str = [](process::State s) {
        return (std::ostringstream{} << s).str();
    };

    BOOST_CHECK_EQUAL(state_str(process::State::STOPPED), "STOPPED");
    BOOST_CHECK_EQUAL(state_str(process::State::STARTING), "STARTING");
    BOOST_CHECK_EQUAL(state_str(process::State::RUNNING), "RUNNING");
    BOOST_CHECK_EQUAL(state_str(process::State::BACKOFF), "BACKOFF");
    BOOST_CHECK_EQUAL(state_str(process::State::STOPPING), "STOPPING");
    BOOST_CHECK_EQUAL(state_str(process::State::EXITED), "EXITED");
    BOOST_CHECK_EQUAL(state_str(process::State::FATAL), "FATAL");
}

BOOST_AUTO_TEST_CASE(xmlrpc__xml_process_info) {
    const auto m = [](auto name, auto val) { return xmlrpc::Member(name, val).toString(); };
    auto serialize = [](const process::ProcessInfo& i) {
        return (std::ostringstream{} << xmlrpc::XmlProcessInfo{i}).str();
    };

    // Basic serialization — all fields present, correct order
    BOOST_CHECK_EQUAL(
        serialize({
            .name = "myapp",
            .state = process::State::RUNNING,
            .pid = 42,
            .exitstatus = 0,
            .stdout_logfile = "/tmp/myapp.log",
            .spawnerr = "",
            .description = "pid 42, uptime 0:01:00",
        }),
        "<struct>"
            + m("name",            "myapp")
            + m("group",           "myapp")  // group mirrors name (groups not supported)
            + m("statename",       "RUNNING")
            + m("state",           20)
            + m("pid",             42)
            + m("exitstatus",      0)
            + m("stdout_logfile",  "/tmp/myapp.log")
            + m("spawnerr",        "")
            + m("description",     "pid 42, uptime 0:01:00")
            + "</struct>");

    // XML escaping — special chars in name and description
    BOOST_CHECK_EQUAL(
        serialize({
            .name = "test<app>",
            .state = process::State::FATAL,
            .pid = 0,
            .exitstatus = 1,
            .description = "error: <&> failed",
        }),
        "<struct>"
            + m("name",            "test<app>")
            + m("group",           "test<app>")
            + m("statename",       "FATAL")
            + m("state",           200)
            + m("pid",             0)
            + m("exitstatus",      1)
            + m("stdout_logfile",  "")
            + m("spawnerr",        "")
            + m("description",     "error: <&> failed")
            + "</struct>");
}
