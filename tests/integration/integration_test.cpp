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
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace supervisord;

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

BOOST_AUTO_TEST_SUITE(IntegrationTests)

// Test 1: Process that exits immediately (startsecs validation)
BOOST_AUTO_TEST_CASE(ProcessExitsImmediately) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_1.sock

[supervisord]
logfile=/tmp/test_integration_1.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_1.sock

[program:exit_immediately]
command=/bin/true
autorestart=false
startsecs=1
stdout_logfile=/tmp/test_exit_immediately.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Run for 3 seconds to allow process to start and exit
    boost::asio::steady_timer timer(io_context);
    timer.expires_after(std::chrono::seconds(3));
    timer.async_wait([&](const boost::system::error_code&) {
        io_context.stop();
    });

    io_context.run();

    auto info = pm.get_all_process_info();
    BOOST_REQUIRE_EQUAL(info.size(), 1);

    // Process should be in EXITED or FATAL state (not RUNNING)
    BOOST_CHECK(info[0].state_code == 100 || info[0].state_code == 200);

    // Cleanup
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
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    // Start process
    pm.start_all();

    // Run briefly to let process start
    boost::asio::steady_timer timer1(io_context);
    timer1.expires_after(std::chrono::seconds(2));
    timer1.async_wait([&](const boost::system::error_code&) {
        auto info = pm.get_all_process_info();
        BOOST_REQUIRE_EQUAL(info.size(), 1);
        BOOST_CHECK_EQUAL(info[0].state_code, 20); // RUNNING

        // Stop process
        pm.stop_process("long_running");

        // Check after stop (may be STOPPING or STOPPED)
        boost::asio::steady_timer timer2(io_context);
        timer2.expires_after(std::chrono::seconds(2));
        timer2.async_wait([&](const boost::system::error_code&) {
            auto info2 = pm.get_all_process_info();
            // Should be STOPPED (0) or STOPPING (40)
            BOOST_CHECK(info2[0].state_code == 0 || info2[0].state_code == 40);

            // Restart
            pm.start_process("long_running");

            boost::asio::steady_timer timer3(io_context);
            timer3.expires_after(std::chrono::seconds(3));
            timer3.async_wait([&](const boost::system::error_code&) {
                auto info3 = pm.get_all_process_info();
                // Should be RUNNING (20) or STARTING (10)
                BOOST_CHECK(info3[0].state_code == 20 || info3[0].state_code == 10);

                pm.stop_all();
                io_context.stop();
            });
        });
    });

    io_context.run();

    // Cleanup
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
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    boost::asio::steady_timer timer(io_context);
    timer.expires_after(std::chrono::seconds(2));
    timer.async_wait([&](const boost::system::error_code&) {
        auto info = pm.get_all_process_info();
        BOOST_CHECK_EQUAL(info.size(), 3);

        // All should be running
        int running_count = 0;
        for (const auto& proc : info) {
            if (proc.state_code == 20) {
                running_count++;
            }
        }
        BOOST_CHECK_EQUAL(running_count, 3);

        pm.stop_all();
        io_context.stop();
    });

    io_context.run();

    // Cleanup
    fs::remove("/tmp/test_integration_socket_3.sock");
    fs::remove("/tmp/test_integration_3.log");
    fs::remove("/tmp/test_proc1.log");
    fs::remove("/tmp/test_proc2.log");
    fs::remove("/tmp/test_proc3.log");
}

// Test 4: Autorestart with failing process
BOOST_AUTO_TEST_CASE(AutorestartFailingProcess) {
    std::string config = R"(
[unix_http_server]
file=/tmp/test_integration_socket_4.sock

[supervisord]
logfile=/tmp/test_integration_4.log

[supervisorctl]
serverurl=unix:///tmp/test_integration_socket_4.sock

[program:fail_fast]
command=/bin/false
autorestart=true
startsecs=0
startretries=3
stdout_logfile=/tmp/test_fail_fast.log
)";

    TempTestConfig temp(config);
    auto parsed_config = config::ConfigParser::parse_file(temp.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Run for 5 seconds to allow retries
    boost::asio::steady_timer timer(io_context);
    timer.expires_after(std::chrono::seconds(5));
    timer.async_wait([&](const boost::system::error_code&) {
        auto info = pm.get_all_process_info();
        BOOST_REQUIRE_EQUAL(info.size(), 1);

        // After 3 retries, should be in FATAL state
        BOOST_CHECK_EQUAL(info[0].state_code, 200); // FATAL

        io_context.stop();
    });

    io_context.run();

    // Cleanup
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
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Run for 5 seconds to allow process to complete
    boost::asio::steady_timer timer(io_context);
    timer.expires_after(std::chrono::seconds(5));
    timer.async_wait([&](const boost::system::error_code&) {
        io_context.stop();
    });

    io_context.run();

    // Check that log file was created and has content
    BOOST_CHECK(fs::exists("/tmp/test_echo.log"));

    if (fs::exists("/tmp/test_echo.log")) {
        std::ifstream log("/tmp/test_echo.log");
        std::string content((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());

        BOOST_CHECK(content.find("Hello from process") != std::string::npos);
        BOOST_CHECK(content.find("Goodbye from process") != std::string::npos);
    }

    // Cleanup
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
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    boost::asio::steady_timer timer(io_context);
    timer.expires_after(std::chrono::seconds(3));
    timer.async_wait([&](const boost::system::error_code&) {
        io_context.stop();
    });

    io_context.run();

    // Check that log shows /tmp as working directory
    if (fs::exists("/tmp/test_pwd.log")) {
        std::ifstream log("/tmp/test_pwd.log");
        std::string content((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());

        BOOST_CHECK(content.find("/tmp") != std::string::npos);
    }

    // Cleanup
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
    process::ProcessManager pm(io_context);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    // Perform 5 rapid start/stop cycles
    int cycle = 0;
    std::function<void()> do_cycle;
    do_cycle = [&]() {
        if (cycle >= 5) {
            io_context.stop();
            return;
        }

        pm.start_process("rapid_test");

        boost::asio::steady_timer timer(io_context);
        timer.expires_after(std::chrono::milliseconds(500));
        timer.async_wait([&](const boost::system::error_code&) {
            pm.stop_process("rapid_test");

            boost::asio::steady_timer timer2(io_context);
            timer2.expires_after(std::chrono::milliseconds(500));
            timer2.async_wait([&](const boost::system::error_code&) {
                cycle++;
                do_cycle();
            });
        });
    };

    do_cycle();
    io_context.run();

    // Process should be stopped (or stopping) and no crashes
    auto info = pm.get_all_process_info();
    // May be STOPPED (0) or STOPPING (40) due to timing
    BOOST_CHECK(info[0].state_code == 0 || info[0].state_code == 40);

    // Cleanup
    fs::remove("/tmp/test_integration_socket_7.sock");
    fs::remove("/tmp/test_integration_7.log");
    fs::remove("/tmp/test_rapid.log");
}

BOOST_AUTO_TEST_SUITE_END()
