// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#define BOOST_TEST_MODULE ConfigParserTest
#include <boost/test/unit_test.hpp>
#include "config/config_parser.h"
#include "util/test_util.h"
#include <fstream>
#include <filesystem>

using namespace supervisorcpp::config;
namespace fs = std::filesystem;

using supervisorcpp::logger::LogLevel;

BOOST_AUTO_TEST_SUITE(ConfigParserTests)

BOOST_AUTO_TEST_CASE(TestParseBasicConfig) {
    std::string config_str = R"(
[unix_http_server]
file=/run/supervisord.sock

[supervisord]
logfile=/var/log/supervisord.log
loglevel=info
user=root
childlogdir=/var/log/supervisor

[supervisorctl]
serverurl=unix:///run/supervisord.sock

[program:test_app]
command=/bin/echo test_app
)";

    Configuration config = ConfigParser::parse_string(config_str);

    // Check unix_http_server
    BOOST_CHECK_EQUAL(config.unix_http_server.socket_file.string(), "/run/supervisord.sock");

    // Check supervisord
    BOOST_CHECK_EQUAL(config.supervisord.logfile.string(), "/var/log/supervisord.log");
    BOOST_CHECK(config.supervisord.loglevel == LogLevel::INFO);
    BOOST_CHECK_EQUAL(config.supervisord.user, "root");
    BOOST_CHECK_EQUAL(config.supervisord.childlogdir.string(), "/var/log/supervisor");
    BOOST_CHECK_EQUAL(config.supervisord.pidfile.string(), "");

    // Check program
    BOOST_CHECK_EQUAL(config.programs.size(), 1);
    BOOST_CHECK_EQUAL(config.programs[0].name, "test_app");
    BOOST_CHECK_EQUAL(config.programs[0].command, "/bin/echo test_app");
}

BOOST_AUTO_TEST_CASE(TestInlineCommentStripping) {
    // Mimics real supervisord.conf with inline ; comments
    std::string config_str = R"(
[unix_http_server]
file=/run/supervisord.sock

[supervisord]
logfile=/var/log/supervisord.log ; (main log file;default $CWD/supervisord.log)
loglevel=info                ; (log level;default info; others: debug,warn,trace)
childlogdir=/var/log/supervisor ; ('AUTO' child log dir, default $TEMP)

[supervisorctl]
serverurl=unix:///run/supervisord.sock

[program:test_app]
command=/bin/echo hello
startsecs=10 ; ten seconds
startretries=2 ; last)";

    Configuration config = ConfigParser::parse_string(config_str);

    BOOST_CHECK(config.supervisord.loglevel == LogLevel::INFO);
    BOOST_CHECK_EQUAL(config.supervisord.logfile.string(), "/var/log/supervisord.log");
    BOOST_CHECK_EQUAL(config.supervisord.childlogdir.string(), "/var/log/supervisor");
    BOOST_REQUIRE_EQUAL(config.programs.size(), 1);
    BOOST_CHECK_EQUAL(config.programs[0].startsecs, 10);
    BOOST_CHECK_EQUAL(config.programs[0].startretries, 2);
}

BOOST_AUTO_TEST_CASE(TestParseProgramConfig) {
    std::string config_str = R"(
[unix_http_server]
file=/run/supervisord.sock

[program:my_app]
command=/bin/sleep 60
environment=LD_LIBRARY_PATH=/opt/apps/lib,DEBUG=1
directory=/tmp
autorestart=true
user=root
stdout_logfile=/var/log/%(program_name)s.log
stdout_logfile_maxbytes=10MB
redirect_stderr=true
startsecs=5
startretries=10
stopwaitsecs=20
stopsignal=INT
)";

    Configuration config = ConfigParser::parse_string(config_str);

    BOOST_REQUIRE_EQUAL(config.programs.size(), 1);
    const auto& prog = config.programs[0];

    BOOST_CHECK_EQUAL(prog.name, "my_app");
    BOOST_CHECK_EQUAL(prog.command, "/bin/sleep 60");
    BOOST_CHECK_EQUAL(prog.directory.value(), "/tmp");
    BOOST_CHECK_EQUAL(prog.autorestart, true);
    BOOST_CHECK_EQUAL(prog.user, "root");

    // Check variable substitution
    BOOST_CHECK_EQUAL(prog.stdout_log.file.value(), std::filesystem::weakly_canonical("/var/log/my_app.log"));

    // Check size parsing
    BOOST_CHECK_EQUAL(prog.stdout_log.file_maxbytes, 10 * 1024 * 1024);

    BOOST_CHECK_EQUAL(prog.redirect_stderr, true);
    BOOST_CHECK_EQUAL(prog.startsecs, 5);
    BOOST_CHECK_EQUAL(prog.startretries, 10);
    BOOST_CHECK_EQUAL(prog.stopwaitsecs, 20);
    BOOST_CHECK_EQUAL(prog.stopsignal, "INT");

    // Check environment parsing
    BOOST_CHECK_EQUAL(prog.environment.size(), 2);
    BOOST_CHECK_EQUAL(prog.environment.at("LD_LIBRARY_PATH"), "/opt/apps/lib");
    BOOST_CHECK_EQUAL(prog.environment.at("DEBUG"), "1");
}

BOOST_AUTO_TEST_CASE(TestParseMultiplePrograms) {
    std::string config_str = R"(
[unix_http_server]
file=/run/supervisord.sock

[program:app1]
command=/bin/echo app1

[program:app2]
command=/bin/echo app2

[program:app3]
command=/bin/echo app3
)";

    Configuration config = ConfigParser::parse_string(config_str);

    BOOST_CHECK_EQUAL(config.programs.size(), 3);
    BOOST_CHECK_EQUAL(config.programs[0].name, "app1");
    BOOST_CHECK_EQUAL(config.programs[1].name, "app2");
    BOOST_CHECK_EQUAL(config.programs[2].name, "app3");
}

BOOST_AUTO_TEST_CASE(TestParseSizeStrings) {
    BOOST_CHECK_EQUAL(parse_size("100"), 100);
    BOOST_CHECK_EQUAL(parse_size("10KB"), 10 * 1024);
    BOOST_CHECK_EQUAL(parse_size("5MB"), 5 * 1024 * 1024);
    BOOST_CHECK_EQUAL(parse_size("1GB"), 1024 * 1024 * 1024);
    BOOST_CHECK_EQUAL(parse_size("10kb"), 10 * 1024);
    BOOST_CHECK_EQUAL(parse_size("5mb"), 5 * 1024 * 1024);
}

BOOST_AUTO_TEST_CASE(TestParseLogLevel) {
    using supervisorcpp::logger::parse_log_level;

    BOOST_CHECK(parse_log_level("debug") == LogLevel::DEBUG);
    BOOST_CHECK(parse_log_level("info") == LogLevel::INFO);
    BOOST_CHECK(parse_log_level("warn") == LogLevel::WARN);
    BOOST_CHECK(parse_log_level("error") == LogLevel::ERROR);
    BOOST_CHECK(parse_log_level("DEBUG") == LogLevel::DEBUG);
    BOOST_CHECK(parse_log_level("INFO") == LogLevel::INFO);
    // Invalid log level now throws exception instead of defaulting
    BOOST_CHECK_THROW(parse_log_level("invalid"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(TestVariableSubstitution) {
    ProgramConfig prog;
    prog.name = "my_test_app";

    std::string input = "/var/log/%(program_name)s.log";
    std::string expected = "/var/log/my_test_app.log";

    BOOST_CHECK_EQUAL(prog.substitute_variables(input), expected);

    // Multiple substitutions
    input = "%(program_name)s-%(program_name)s.log";
    expected = "my_test_app-my_test_app.log";

    BOOST_CHECK_EQUAL(prog.substitute_variables(input), expected);
}

BOOST_AUTO_TEST_CASE(TestFindProgram) {
    Configuration config;

    ProgramConfig prog1;
    prog1.name = "app1";
    prog1.command = "/usr/bin/app1";

    ProgramConfig prog2;
    prog2.name = "app2";
    prog2.command = "/usr/bin/app2";

    config.programs.push_back(prog1);
    config.programs.push_back(prog2);

    const ProgramConfig* found = config.find_program("app1");
    BOOST_REQUIRE(found != nullptr);
    BOOST_CHECK_EQUAL(found->name, "app1");

    found = config.find_program("app2");
    BOOST_REQUIRE(found != nullptr);
    BOOST_CHECK_EQUAL(found->name, "app2");

    found = config.find_program("nonexistent");
    BOOST_CHECK(found == nullptr);
}

BOOST_AUTO_TEST_CASE(TestMissingRequiredCommand) {
    std::string config_str = R"(
[unix_http_server]
file=/run/supervisord.sock

[program:bad_app]
directory=/tmp
)";

    BOOST_CHECK_THROW(ConfigParser::parse_string(config_str), ConfigParseError);
}

BOOST_AUTO_TEST_CASE(TestParseFileNotFound) {
    BOOST_CHECK_THROW(
        ConfigParser::parse_file("/nonexistent/config.ini"),
        ConfigParseError
    );
}

BOOST_AUTO_TEST_CASE(TestParseActualFile) {
    // This test requires the test data file to exist
    auto exe_dir = fs::canonical(boost::unit_test::framework::master_test_suite().argv[0]).parent_path();
    fs::path test_file = exe_dir / "tests" / "data" / "test_config.ini";

    if (fs::exists(test_file)) {
        Configuration config = ConfigParser::parse_file(test_file);

        BOOST_CHECK_EQUAL(config.unix_http_server.socket_file.string(), "/tmp/test_supervisor.sock");
        BOOST_CHECK_EQUAL(config.supervisord.loglevel, LogLevel::DEBUG);
        BOOST_CHECK_EQUAL(config.supervisord.user, "testuser");
        BOOST_CHECK_EQUAL(config.supervisord.pidfile.string(), "/tmp/test_supervisord.pid");

        BOOST_REQUIRE_EQUAL(config.programs.size(), 1);
        const auto& prog = config.programs[0];

        BOOST_CHECK_EQUAL(prog.name, "test_app");
        BOOST_CHECK_EQUAL(prog.command, "/bin/echo test_app");
        BOOST_CHECK_EQUAL(prog.environment.at("FOO"), "bar");
        BOOST_CHECK_EQUAL(prog.environment.at("BAZ"), "qux");
        BOOST_CHECK_EQUAL(prog.stdout_log.file.value(), std::filesystem::weakly_canonical("/tmp/test_app.log"));
    }
}

BOOST_AUTO_TEST_CASE(TestUmaskParsing) {
    // Daemon-level umask
    {
        const auto config = ConfigParser::parse_string(R"(
[supervisord]
logfile=/var/log/supervisord.log
umask=077
[program:test]
command=)" + test_util::true_exe());
        BOOST_CHECK_EQUAL(config.supervisord.umask, 077);
    }

    // Default umask (022) when not specified
    {
        const auto config = ConfigParser::parse_string(R"(
[unix_http_server]
file=/run/supervisord.sock
[supervisord]
logfile=/var/log/supervisord.log
[program:test]
command=)" + test_util::true_exe());
        BOOST_CHECK_EQUAL(config.supervisord.umask, 022);
    }

    // Per-process umask
    {
        const auto config = ConfigParser::parse_string(R"(
[unix_http_server]
file=/run/supervisord.sock
[supervisord]
logfile=/var/log/supervisord.log
[program:test]
umask=027
command=)" + test_util::true_exe());
        BOOST_REQUIRE(config.programs[0].umask.has_value());
        BOOST_CHECK_EQUAL(*config.programs[0].umask, 027);
    }

    // Per-process umask not set
    {
        const auto config = ConfigParser::parse_string(R"(
[unix_http_server]
file=/run/supervisord.sock
[supervisord]
logfile=/var/log/supervisord.log
[program:test]
command=)" + test_util::true_exe());
        BOOST_CHECK(!config.programs[0].umask.has_value());
    }
}

BOOST_AUTO_TEST_SUITE_END()
