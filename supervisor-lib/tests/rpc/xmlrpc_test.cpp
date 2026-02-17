// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#define BOOST_TEST_MODULE XmlRpcTest
#include <boost/test/unit_test.hpp>
#include "process/process.h"
#include "rpc/xmlrpc.h"
#include <sstream>

namespace process = supervisorcpp::process;
namespace xmlrpc = supervisorcpp::xmlrpc;

BOOST_AUTO_TEST_CASE(xmlrpc__value) {
    BOOST_CHECK_EQUAL(xmlrpc::Value{"Test"}.str(), "<string>Test</string>");

    BOOST_CHECK_EQUAL(xmlrpc::Value{42}.str(), "<int>42</int>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{-2}.str(), "<int>-2</int>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{0}.str(), "<int>0</int>");

    BOOST_CHECK_EQUAL(xmlrpc::Value{true}.str(), "<boolean>1</boolean>");
    BOOST_CHECK_EQUAL(xmlrpc::Value{false}.str(), "<boolean>0</boolean>");

    BOOST_CHECK_EQUAL(xmlrpc::Member("ABoolean", true).str(), "<member><name>ABoolean</name><value><boolean>1</boolean></value></member>");
    BOOST_CHECK_EQUAL(xmlrpc::Member("Beg<>End", "Beg & End").str(), "<member><name>Beg&lt;&gt;End</name><value><string>Beg &amp; End</string></value></member>");
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
    using namespace xmlrpc;

    // Basic serialization — all fields present, correct order
    {
        process::ProcessInfo info;
        info.name = "myapp";
        info.state = process::State::RUNNING;
        info.pid = 42;
        info.exitstatus = 0;
        info.stdout_logfile = "/tmp/myapp.log";
        info.spawnerr = "";
        info.description = "pid 42, uptime 0:01:00";
        BOOST_CHECK_EQUAL(
            wrap(info).str(),
            Struct({
                Member("name",            "myapp"),
                Member("group",           "myapp"),  // group mirrors name (groups not supported)
                Member("statename",       "RUNNING"),
                Member("state",           20),
                Member("pid",             42),
                Member("exitstatus",      0),
                Member("stdout_logfile",  "/tmp/myapp.log"),
                Member("spawnerr",        ""),
                Member("description",     "pid 42, uptime 0:01:00"),
            }).str()
        );
    }

    // XML escaping — special chars in name and description
    {
        process::ProcessInfo info;
        info.name = "test<app>";
        info.state = process::State::FATAL;
        info.pid = 0;
        info.exitstatus = 1;
        info.description = "error: <&> failed";

        BOOST_CHECK_EQUAL(
            wrap(info).str(),
            Struct({
                Member("name",            "test<app>"),
                Member("group",           "test<app>"),
                Member("statename",       "FATAL"),
                Member("state",           200),
                Member("pid",             0),
                Member("exitstatus",      1),
                Member("stdout_logfile",  ""),
                Member("spawnerr",        ""),
                Member("description",     "error: <&> failed"),
            }).str()
        );
    }
}
