// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

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
#include "logger/logger.h"
#include "process/process.h"
#include "process/process_manager.h"
#include "rpc/rpc_server.h"
#include "util/test_util.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <boost/asio.hpp>

namespace fs = std::filesystem;
using namespace supervisorcpp;
using namespace std::chrono_literals;

using TempManager = test_util::TempManager;

// Run io_context until condition is met or timeout expires.
// Returns true if condition was met, false on timeout.
bool poll_until(boost::asio::io_context& io,
                std::function<bool()> condition,
                std::chrono::milliseconds timeout = 5000ms,
                std::chrono::milliseconds interval = 50ms)
{
    boost::asio::steady_timer timer{io};
    bool met = false;

    std::function<void()> poll;
    poll = [
        start{std::chrono::steady_clock::now()},
        &condition, interval, &io, &met, &poll, timeout, &timer
    ]() {
        met = condition();
        if (met || std::chrono::steady_clock::now() - start >= timeout) {
            io.stop();
            return;
        }
        timer.expires_after(interval);
        timer.async_wait([&poll](const boost::system::error_code& ec) {
            if (!ec) poll();
        });
    };
    poll();

    io.run();
    io.restart();

    return met;
}

bool poll_until_state(
    boost::asio::io_context& io,
    const process::ProcessManager& pm,
    process::State state,
    std::chrono::milliseconds timeout = 5000ms,
    std::chrono::milliseconds interval = 50ms
) {
    return poll_until(io, [&pm, state] {
        const auto info = pm.get_all_process_info();
        return std::all_of(std::begin(info), std::end(info), [state](const auto& i) {
            return (i.state == state);
        });
    }, timeout, interval);
}


BOOST_AUTO_TEST_SUITE(IntegrationTests)

// Test 1: Process that exits immediately (startsecs validation)
BOOST_AUTO_TEST_CASE(ProcessExitsImmediately) {
    const auto sock = TempManager::file("sock_1.sock");
    const auto log1 = TempManager::file("supervisord_1.log");
    const auto log2 = TempManager::file("exit_immediately.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:exit_immediately]
command=)" + test_util::true_exe() + R"(
autorestart=false
startsecs=1
stdout_logfile=)" + log2.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    BOOST_CHECK(
        poll_until(io_context, [&pm] {
            auto info = pm.get_all_process_info();
            return !info.empty() && (info[0].state == process::State::EXITED || info[0].state == process::State::FATAL);
        })
    );
}

// Test 2: Process start/stop/restart cycle
BOOST_AUTO_TEST_CASE(ProcessStartStopRestart) {
    const auto sock = TempManager::file("sock_2.sock");
    const auto log1 = TempManager::file("supervisord_2.log");
    const auto log2 = TempManager::file("long_running.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:long_running]
command=/bin/sleep 60
autorestart=false
startsecs=1
stdout_logfile=)" + log2.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    // Start and wait for RUNNING
    pm.start_all();
    BOOST_CHECK(poll_until_state(io_context, pm, process::State::RUNNING));

    // Stop and wait for STOPPED
    pm.stop_process("long_running");
    BOOST_CHECK(poll_until_state(io_context, pm, process::State::STOPPED));

    // Restart and wait for RUNNING
    pm.start_process("long_running");
    BOOST_CHECK(poll_until_state(io_context, pm, process::State::RUNNING));

    pm.stop_all();
    BOOST_CHECK(poll_until_state(io_context, pm, process::State::STOPPED));
}

// Test 3: Multiple processes managed simultaneously
BOOST_AUTO_TEST_CASE(MultipleProcesses) {
    const auto sock = TempManager::file("sock_3.sock");
    const auto log1 = TempManager::file("supervisord_3.log");
    const auto log2 = TempManager::file("proc1.log");
    const auto log3 = TempManager::file("proc2.log");
    const auto log4 = TempManager::file("proc3.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:proc1]
command=/bin/sleep 10
autorestart=false
stdout_logfile=)" + log2.str() + R"(

[program:proc2]
command=/bin/sleep 10
autorestart=false
stdout_logfile=)" + log3.str() + R"(

[program:proc3]
command=/bin/sleep 10
autorestart=false
stdout_logfile=)" + log4.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();
    BOOST_CHECK_EQUAL(pm.get_all_process_info().size(), 3);
    BOOST_CHECK(poll_until_state(io_context, pm, process::State::RUNNING));

    pm.stop_all();
    BOOST_CHECK(poll_until_state(io_context, pm, process::State::STOPPED));
}

// Test 4: Autorestart with failing process
BOOST_AUTO_TEST_CASE(AutorestartFailingProcess) {
    const auto sock = TempManager::file("sock_4.sock");
    const auto log1 = TempManager::file("supervisord_4.log");
    const auto log2 = TempManager::file("fail_fast.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:fail_fast]
command=)" + test_util::false_exe() + R"(
autorestart=true
startsecs=1
startretries=3
stdout_logfile=)" + log2.str() + "\n");

    auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Retries with backoff may take a few seconds
    BOOST_CHECK( poll_until_state(io_context, pm, process::State::FATAL, 10000ms) );
}

// Test 5: Log capture validation
BOOST_AUTO_TEST_CASE(LogCaptureValidation) {
    const auto sock = TempManager::file("sock_5.sock");
    const auto log1 = TempManager::file("supervisord_5.log");
    const auto log2 = TempManager::file("echo.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:echo_test]
command=/bin/sh -c "echo 'Hello from process'; sleep 2; echo 'Goodbye from process'"
autorestart=false
startsecs=0
stdout_logfile=)" + log2.str() + "\n");

    auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Process itself takes ~2s (has sleep 2), poll until log has final output
    const auto& echo_log = log2.path();
    const bool reached = poll_until(io_context, [&echo_log] {
        const auto content = test_util::read_file(echo_log);
        return content.find("Goodbye from process") != std::string::npos;
    }, 10000ms);

    BOOST_CHECK(reached);

    const auto content = test_util::read_file(echo_log);
    BOOST_CHECK(content.find("Hello from process") != std::string::npos);
    BOOST_CHECK(content.find("Goodbye from process") != std::string::npos);
}

// Test 6: Process with working directory
BOOST_AUTO_TEST_CASE(ProcessWithWorkingDirectory) {
    const auto sock = TempManager::file("sock_6.sock");
    const auto log1 = TempManager::file("supervisord_6.log");
    const auto log2 = TempManager::file("pwd.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:pwd_test]
command=/bin/sh -c "pwd"
directory=/tmp
autorestart=false
startsecs=0
stdout_logfile=)" + log2.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    const auto& pwd_log = log2.path();
    const bool reached = poll_until(io_context, [&pwd_log] {
        if (!fs::exists(pwd_log)) return false;
        const auto content = test_util::read_file(pwd_log);
        return content.find("/tmp") != std::string::npos;
    });

    BOOST_CHECK(reached);

    const auto content = test_util::read_file(pwd_log);
    BOOST_CHECK(content.find("/tmp") != std::string::npos);
}

// Test 7: Rapid start/stop cycles (stress test)
BOOST_AUTO_TEST_CASE(RapidStartStopCycles) {
    const auto sock = TempManager::file("sock_7.sock");
    const auto log1 = TempManager::file("supervisord_7.log");
    const auto log2 = TempManager::file("rapid.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:rapid_test]
command=/bin/sleep 30
autorestart=false
startsecs=1
stdout_logfile=)" + log1.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    for (int cycle = 0; cycle < 5; cycle++) {
        pm.start_process("rapid_test");

        // Wait until process is at least STARTING
        poll_until(io_context, [&pm] {
            const auto info = pm.get_all_process_info();
            return !info.empty() && info[0].state >= process::State::STARTING;
        }, 2000ms);

        pm.stop_process("rapid_test");

        // Wait until fully STOPPED before next cycle
        poll_until_state(io_context, pm, process::State::STOPPED, 3000ms);
    }

    const auto info = pm.get_all_process_info();
    BOOST_REQUIRE_EQUAL(info.size(), 1);
    BOOST_CHECK(info[0].state == process::State::STOPPED || info[0].state == process::State::STOPPING);
}

// Test 8: Separate stderr log capture
BOOST_AUTO_TEST_CASE(StderrLogCapture) {
    const auto sock = TempManager::file("sock_8.sock");
    const auto log1 = TempManager::file("supervisord_8.log");
    const auto stdout_log = TempManager::file("stderr_test_stdout.log");
    const auto stderr_log = TempManager::file("stderr_test_stderr.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:stderr_test]
command=/bin/sh -c "echo 'stdout message'; echo 'stderr message' >&2"
autorestart=false
startsecs=0
stdout_logfile=)" + stdout_log.str() + R"(
stderr_logfile=)" + stderr_log.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Wait for both logs to have content
    const auto& out_path = stdout_log.path();
    const auto& err_path = stderr_log.path();
    const bool reached = poll_until(io_context, [&out_path, &err_path] {
        const auto out = test_util::read_file(out_path);
        const auto err = test_util::read_file(err_path);
        return out.find("stdout message") != std::string::npos
            && err.find("stderr message") != std::string::npos;
    }, 10000ms);

    BOOST_CHECK(reached);

    // Verify stdout only in stdout log, stderr only in stderr log
    const auto stdout_content = test_util::read_file(out_path);
    const auto stderr_content = test_util::read_file(err_path);
    BOOST_CHECK(stdout_content.find("stdout message") != std::string::npos);
    BOOST_CHECK(stderr_content.find("stderr message") != std::string::npos);
    BOOST_CHECK(stdout_content.find("stderr message") == std::string::npos);
    BOOST_CHECK(stderr_content.find("stdout message") == std::string::npos);
}

// Test 9: redirect_stderr merges into stdout log (no separate stderr file)
BOOST_AUTO_TEST_CASE(RedirectStderrToStdout) {
    const auto sock = TempManager::file("sock_9.sock");
    const auto log1 = TempManager::file("supervisord_9.log");
    const auto stdout_log = TempManager::file("redirect_test.log");

    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:redirect_test]
command=/bin/sh -c "echo 'out line'; echo 'err line' >&2"
autorestart=false
startsecs=0
redirect_stderr=true
stdout_logfile=)" + stdout_log.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    const auto& out_path = stdout_log.path();
    const bool reached = poll_until(io_context, [&out_path] {
        const auto content = test_util::read_file(out_path);
        return content.find("out line") != std::string::npos
            && content.find("err line") != std::string::npos;
    }, 10000ms);

    BOOST_CHECK(reached);

    const auto content = test_util::read_file(out_path);
    BOOST_CHECK(content.find("out line") != std::string::npos);
    BOOST_CHECK(content.find("err line") != std::string::npos);
}

// Test 10: Per-process umask affects child file creation
BOOST_AUTO_TEST_CASE(PerProcessUmask) {
    const auto sock = TempManager::file("sock_10.sock");
    const auto log1 = TempManager::file("supervisord_10.log");
    const auto stdout_log = TempManager::file("umask_test.log");
    const auto test_file = TempManager::file("umask_created.txt");

    // umask 077 → files created with mode 0600 (0666 & ~077)
    const auto cfg = TempManager::config(R"(
[unix_http_server]
file=)" + sock.str() + R"(

[supervisord]
logfile=)" + log1.str() + R"(

[program:umask_test]
command=/bin/sh -c "touch )" + test_file.str() + R"("
autorestart=false
startsecs=0
umask=077
stdout_logfile=)" + stdout_log.str() + "\n");

    const auto parsed_config = config::ConfigParser::parse_file(cfg.path());

    boost::asio::io_context io_context;
    process::ProcessManager pm(io_context, 100ms);

    for (const auto& prog : parsed_config.programs) {
        pm.add_process(prog);
    }

    pm.start_all();

    // Wait for the file to be created
    const auto& created_path = test_file.path();
    const bool reached = poll_until(io_context, [&created_path] {
        return fs::exists(created_path);
    }, 10000ms);

    BOOST_CHECK(reached);

    // Check file permissions: umask 077 → 0600
    struct stat st;
    BOOST_REQUIRE_EQUAL(stat(test_file.c_str(), &st), 0);
    const auto perms = st.st_mode & 0777;
    BOOST_CHECK_EQUAL(perms, 0600);
}

// Test 11: sync_processes adds new and removes old processes
BOOST_AUTO_TEST_CASE(SyncProcessesAddsAndRemoves) {
    const auto sock = TempManager::file("sock_11.sock");
    const auto log1 = TempManager::file("supervisord_11.log");

    boost::asio::io_context io_context;
    process::ProcessManager pm{io_context, 100ms};
    const auto pm_pid_for = [&pm](const std::string& name) {
        const auto process_ptr = pm.get_process(name);
        return (process_ptr) ? process_ptr->pid() : -1;
    };

    // Start with alpha
    config::ProgramConfig alpha;
    alpha.name = "alpha";
    alpha.command = "/bin/sleep 60";
    pm.add_process(alpha);

    pm.start_all();
    BOOST_CHECK_NE(pm_pid_for("alpha"), -1);
    BOOST_CHECK_EQUAL(pm_pid_for("beta"), -1);

    // now re-sync with the same config
    const auto pid_alpha_before = pm_pid_for("alpha");
    pm.sync_processes({ alpha });

    // no change
    BOOST_CHECK_EQUAL(pm_pid_for("alpha"), pid_alpha_before);
    BOOST_CHECK_EQUAL(pm_pid_for("beta"), -1);


    // Sync with beta only — alpha should be removed, beta added
    config::ProgramConfig beta;
    beta.name = "beta";
    beta.command = "/bin/sleep 60";

    pm.sync_processes({beta});

    BOOST_CHECK_EQUAL(pm_pid_for("alpha"), -1);
    BOOST_CHECK_NE(pm_pid_for("beta"), -1);


    // now re-sync with the same config
    const auto pid_beta_before = pm_pid_for("beta");
    pm.sync_processes({beta});

    // no change
    BOOST_CHECK_EQUAL(pm_pid_for("alpha"), -1);
    BOOST_CHECK_EQUAL(pm_pid_for("beta"), pid_beta_before);


    // re-add alpha
    pm.sync_processes({ alpha, beta });
    BOOST_CHECK_NE(pm_pid_for("alpha"), -1);
    BOOST_CHECK_NE(pm_pid_for("alpha"), pid_alpha_before);
    BOOST_CHECK_EQUAL(pm_pid_for("beta"), pid_beta_before);

    // remove them all
    pm.sync_processes({ });
    BOOST_CHECK_EQUAL(pm_pid_for("alpha"), -1);
    BOOST_CHECK_EQUAL(pm_pid_for("beta"), -1);

    pm.stop_all();
}

BOOST_AUTO_TEST_SUITE_END()
