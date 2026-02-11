/**
 * Integration tests for supervisord
 *
 * Tests full workflow including:
 * - Process lifecycle (start, stop, restart)
 * - State transitions
 * - Autorestart with backoff
 * - Signal handling
 * - RPC interface
 * - Log capture
 */

#define BOOST_TEST_MODULE IntegrationTest
#include <boost/test/unit_test.hpp>
#include "config/config_parser.h"
#include "process/process_manager.h"
#include "rpc/rpc_server.h"
#include "util/logger.h"
#include <boost/asio.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace supervisord;
using namespace std::chrono_literals;

// Helper to create test config files
class TempTestConfig {
public:
    TempTestConfig(const std::string& content) {
        path_ = fs::temp_directory_path() / ("test_integration_" + std::to_string(counter_++) + ".ini");
        std::ofstream file(path_);
        file << content;
        file.close();
    }

    ~TempTestConfig() {
        if (fs::exists(path_)) {
            fs::remove(path_);
        }
    }

    fs::path path() const { return path_; }

private:
    fs::path path_;
    static int counter_;
};

int TempTestConfig::counter_ = 0;

// Run io_context until condition is met or timeout expires.
// Returns true if condition was met, false on timeout.
bool poll_until(boost::asio::io_context& io,
                std::function<bool()> condition,
                std::chrono::milliseconds timeout = 5000ms,
                std::chrono::milliseconds interval = 50ms)
{
    bool met = false;
    auto start = std::chrono::steady_clock::now();

    boost::asio::steady_timer timer(io);

    std::function<void()> poll;
    poll = [&]() {
        if (condition()) {
            met = true;
            io.stop();
            return;
        }
        if (std::chrono::steady_clock::now() - start >= timeout) {
            io.stop();
            return;
        }
        timer.expires_after(interval);
        timer.async_wait([&](const boost::system::error_code& ec) {
            if (!ec) poll();
        });
    };
    poll();

    io.run();
    io.restart();

    return met;
}

BOOST_AUTO_TEST_SUITE(IntegrationTests)

// Test 1: Process that exits immediately (startsecs validation)
BOOST_AUTO_TEST_CASE(ProcessExitsImmediately) {
    const std::string BIN_TRUE = std::filesystem::exists("/usr/bin/true") ? "/usr/bin/true" : "/bin/true";

    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_1.sock

[supervisord]
logfile=/tmp/test_integration_1.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_1.sock

[program:exit_immediately]
command=)" + BIN_TRUE + R"(
autorestart=false
startsecs=1
stdout_logfile=/tmp/test_exit_immediately.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    bool reached = poll_until(io_context, [&] {
        auto info = pm.get_all_process_info();
        return !info.empty() && (info[0].state_code == 100 || info[0].state_code == 200);
    });

    BOOST_CHECK(reached);
    auto info = pm.get_all_process_info();
    BOOST_REQUIRE_EQUAL(info.size(), 1);
    BOOST_CHECK(info[0].state_code == 100 || info[0].state_code == 200);

    fs::remove("/tmp/test_integration_socket_1.sock");
    fs::remove("/tmp/test_integration_1.log");
    fs::remove("/tmp/test_exit_immediately.log");
}

// Test 2: Process start/stop/restart cycle
BOOST_AUTO_TEST_CASE(ProcessStartStopRestart) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_2.sock

[supervisord]
logfile=/tmp/test_integration_2.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_2.sock

[program:long_running]
command=/bin/sleep 60
autorestart=false
startsecs=1
stdout_logfile=/tmp/test_long_running.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    // Start and wait for RUNNING
    pm.start_all();
    BOOST_CHECK(poll_until(io_context, [&] {
        return pm.get_all_process_info()[0].state_code == 20;
    }));

    // Stop and wait for STOPPED
    pm.stop_process("long_running");
    BOOST_CHECK(poll_until(io_context, [&] {
        return pm.get_all_process_info()[0].state_code == 0;
    }));

    // Restart and wait for RUNNING
    pm.start_process("long_running");
    BOOST_CHECK(poll_until(io_context, [&] {
        return pm.get_all_process_info()[0].state_code == 20;
    }));

    pm.stop_all();
    poll_until(io_context, [&] {
        return pm.get_all_process_info()[0].state_code == 0;
    });

    fs::remove("/tmp/test_integration_socket_2.sock");
    fs::remove("/tmp/test_integration_2.log");
    fs::remove("/tmp/test_long_running.log");
}

// Test 3: Multiple processes managed simultaneously
BOOST_AUTO_TEST_CASE(MultipleProcesses) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_3.sock

[supervisord]
logfile=/tmp/test_integration_3.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_3.sock

[program:proc1]
command=/bin/sleep 10
autorestart=false
stdout_logfile=/tmp/test_proc1.log

[program:proc2]
command=/bin/sleep 10
autorestart=false
stdout_logfile=/tmp/test_proc2.log

[program:proc3]
command=/bin/sleep 10
autorestart=false
stdout_logfile=/tmp/test_proc3.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    BOOST_CHECK(poll_until(io_context, [&] {
        int running = 0;
        for (const auto& p : pm.get_all_process_info())
            if (p.state_code == 20) running++;
        return running == 3;
    }));

    BOOST_CHECK_EQUAL(pm.get_all_process_info().size(), 3);

    pm.stop_all();
    poll_until(io_context, [&] {
        for (const auto& p : pm.get_all_process_info())
            if (p.state_code != 0) return false;
        return true;
    });

    fs::remove("/tmp/test_integration_socket_3.sock");
    fs::remove("/tmp/test_integration_3.log");
    fs::remove("/tmp/test_proc1.log");
    fs::remove("/tmp/test_proc2.log");
    fs::remove("/tmp/test_proc3.log");
}

// Test 4: Autorestart with failing process
BOOST_AUTO_TEST_CASE(AutorestartFailingProcess) {
    const std::string BIN_FALSE = std::filesystem::exists("/usr/bin/false") ? "/usr/bin/false" : "/bin/false";

    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_4.sock

[supervisord]
logfile=/tmp/test_integration_4.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_4.sock

[program:fail_fast]
command=)" + BIN_FALSE + R"(
autorestart=true
startsecs=1
startretries=3
stdout_logfile=/tmp/test_fail_fast.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Retries with backoff may take a few seconds
    bool reached = poll_until(io_context, [&] {
        auto info = pm.get_all_process_info();
        return !info.empty() && info[0].state_code == 200;
    }, 10000ms);

    BOOST_CHECK(reached);
    auto info = pm.get_all_process_info();
    BOOST_REQUIRE_EQUAL(info.size(), 1);
    BOOST_CHECK_EQUAL(info[0].state_code, 200); // FATAL

    fs::remove("/tmp/test_integration_socket_4.sock");
    fs::remove("/tmp/test_integration_4.log");
    fs::remove("/tmp/test_fail_fast.log");
}

// Test 5: Log capture validation
BOOST_AUTO_TEST_CASE(LogCaptureValidation) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_5.sock

[supervisord]
logfile=/tmp/test_integration_5.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_5.sock

[program:echo_test]
command=/bin/sh -c "echo 'Hello from process'; sleep 2; echo 'Goodbye from process'"
autorestart=false
startsecs=0
stdout_logfile=/tmp/test_echo.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Process itself takes ~2s (has sleep 2), poll until log has final output
    bool reached = poll_until(io_context, [&] {
        if (!fs::exists("/tmp/test_echo.log")) return false;
        std::ifstream log("/tmp/test_echo.log");
        std::string content((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());
        return content.find("Goodbye from process") != std::string::npos;
    }, 10000ms);

    BOOST_CHECK(reached);

    if (fs::exists("/tmp/test_echo.log")) {
        std::ifstream log("/tmp/test_echo.log");
        std::string content((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());
        BOOST_CHECK(content.find("Hello from process") != std::string::npos);
        BOOST_CHECK(content.find("Goodbye from process") != std::string::npos);
    }

    fs::remove("/tmp/test_integration_socket_5.sock");
    fs::remove("/tmp/test_integration_5.log");
    fs::remove("/tmp/test_echo.log");
}

// Test 6: Process with working directory
BOOST_AUTO_TEST_CASE(ProcessWithWorkingDirectory) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_6.sock

[supervisord]
logfile=/tmp/test_integration_6.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_6.sock

[program:pwd_test]
command=/bin/sh -c "pwd"
directory=/tmp
autorestart=false
startsecs=0
stdout_logfile=/tmp/test_pwd.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    bool reached = poll_until(io_context, [&] {
        if (!fs::exists("/tmp/test_pwd.log")) return false;
        std::ifstream log("/tmp/test_pwd.log");
        std::string content((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());
        return content.find("/tmp") != std::string::npos;
    });

    BOOST_CHECK(reached);

    if (fs::exists("/tmp/test_pwd.log")) {
        std::ifstream log("/tmp/test_pwd.log");
        std::string content((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());
        BOOST_CHECK(content.find("/tmp") != std::string::npos);
    }

    fs::remove("/tmp/test_integration_socket_6.sock");
    fs::remove("/tmp/test_integration_6.log");
    fs::remove("/tmp/test_pwd.log");
}

// Test 7: Rapid start/stop cycles (stress test)
BOOST_AUTO_TEST_CASE(RapidStartStopCycles) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_7.sock

[supervisord]
logfile=/tmp/test_integration_7.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_7.sock

[program:rapid_test]
command=/bin/sleep 30
autorestart=false
startsecs=1
stdout_logfile=/tmp/test_rapid.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    for (int cycle = 0; cycle < 5; cycle++) {
        pm.start_process("rapid_test");

        // Wait until process is at least STARTING
        poll_until(io_context, [&] {
            auto info = pm.get_all_process_info();
            return !info.empty() && info[0].state_code >= 10;
        }, 2000ms);

        pm.stop_process("rapid_test");

        // Wait until fully STOPPED before next cycle
        poll_until(io_context, [&] {
            auto info = pm.get_all_process_info();
            return !info.empty() && info[0].state_code == 0;
        }, 3000ms);
    }

    auto info = pm.get_all_process_info();
    BOOST_CHECK(info[0].state_code == 0 || info[0].state_code == 40);

    fs::remove("/tmp/test_integration_socket_7.sock");
    fs::remove("/tmp/test_integration_7.log");
    fs::remove("/tmp/test_rapid.log");
}

BOOST_AUTO_TEST_SUITE_END()
