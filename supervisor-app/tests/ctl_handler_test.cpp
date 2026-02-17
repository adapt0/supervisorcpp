/**
 * Integration tests for supervisorctl client interactions.
 * Tests the full XML-RPC roundtrip over a real Unix socket:
 *   client serialization → socket → server dispatch → fault response → client parsing
 */

#define BOOST_TEST_MODULE CtlHandlerTest
#include <boost/test/unit_test.hpp>
#include "ctl/supervisorctl.h"
#include "daemon/rpc_handlers.h"
#include "../../supervisor-lib/tests/test_util.h"
#include "process/process_manager.h"
#include "rpc/rpc_server.h"
#include <thread>
#include <boost/asio.hpp>

namespace rpc = supervisorcpp::rpc;
namespace process = supervisorcpp::process;
using namespace std::chrono_literals;
using TempManager = test_util::TempManager;
using SupervisorCtlClient = supervisorcpp::SupervisorCtlClient;

/// Sleep-poll until condition is met or timeout (does not touch io_context)
static bool poll_until(std::function<bool()> condition,
                       std::chrono::milliseconds timeout = 5000ms) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (condition()) return true;
        std::this_thread::sleep_for(50ms);
    }
    return false;
}

struct CtlFixture {
    TempManager::Cleanup sock = TempManager::file("ctl_test.sock");
    TempManager::Cleanup log1 = TempManager::file("ctl_test_d.log");
    TempManager::Cleanup log2 = TempManager::file("ctl_test_p.log");

    boost::asio::io_context io;
    process::ProcessManager pm{io, 100ms};
    std::shared_ptr<rpc::RpcServer> server = rpc::RpcServer::create(io, sock.str());

    std::ostringstream out;
    std::ostringstream err;

    std::jthread server_thread;

    CtlFixture() {
        supervisorcpp::config::ProgramConfig cfg;
        cfg.name = "testproc";
        cfg.command = "/bin/sleep 60";
        cfg.autorestart = false;
        cfg.startsecs = 1;
        cfg.stdout_log.file = log2.path();
        pm.add_process(cfg);

        supervisorcpp::register_process_handlers(*server, pm);
        server->start();

        // Run io_context in background so server can handle connections
        server_thread = std::jthread([this](std::stop_token) {
            auto work = boost::asio::make_work_guard(io);
            io.run();
        });
    }

    ~CtlFixture() {
        pm.stop_all();
        server->stop();
        io.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    SupervisorCtlClient make_client() {
        out.str("");
        err.str("");
        return SupervisorCtlClient{sock.str(), out, err};
    }

    void check_out(std::string_view expected) {
        BOOST_CHECK_MESSAGE(out.str().contains(expected),
            "expected '" << expected << "' in stdout: \"" << out.str() << "\"");
    }
    void check_err(std::string_view expected) {
        BOOST_CHECK_MESSAGE(err.str().contains(expected),
            "expected '" << expected << "' in stderr: \"" << err.str() << "\"");
    }

    void wait_running() {
        poll_until([this] {
            const auto info = pm.get_all_process_info();
            return !info.empty() && info[0].state == process::State::RUNNING;
        });
    }

    void wait_stopped() {
        poll_until([this] {
            const auto info = pm.get_all_process_info();
            return !info.empty() && info[0].state == process::State::STOPPED;
        });
    }
};


// --- BAD_NAME tests (non-existent process) ---

BOOST_FIXTURE_TEST_CASE(ctl__start_bad_name, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("start", {"nonexistent"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err("no such process");
}

BOOST_FIXTURE_TEST_CASE(ctl__stop_bad_name, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("stop", {"nonexistent"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err("no such process");
}

BOOST_FIXTURE_TEST_CASE(ctl__status_bad_name, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("status", {"nonexistent"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err("no such process");
}

// --- Edge case: stop a stopped process ---

BOOST_FIXTURE_TEST_CASE(ctl__stop_stopped_process, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("stop", {"testproc"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err("not running");
}

// --- Edge case: start an already-running process ---

BOOST_FIXTURE_TEST_CASE(ctl__start_already_running, CtlFixture) {
    pm.start_process("testproc");
    wait_running();

    auto client = make_client();
    const auto rc = client.execute_command("start", {"testproc"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err("already running");
}

// --- Restart of a stopped process (tolerates NOT_RUNNING on stop) ---

BOOST_FIXTURE_TEST_CASE(ctl__restart_stopped_process, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("restart", {"testproc"});
    BOOST_CHECK_EQUAL(rc, 0);
    check_out("restarted");
}

// --- Normal start and stop ---

BOOST_FIXTURE_TEST_CASE(ctl__start_and_stop, CtlFixture) {
    {
        auto client = make_client();
        const auto rc = client.execute_command("start", {"testproc"});
        BOOST_CHECK_EQUAL(rc, 0);
        check_out("started");
    }

    wait_running();

    {
        auto client = make_client();
        const auto rc = client.execute_command("stop", {"testproc"});
        BOOST_CHECK_EQUAL(rc, 0);
        check_out("stopped");
    }
}

// --- Status all ---

BOOST_FIXTURE_TEST_CASE(ctl__status_all, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("status", {});
    BOOST_CHECK_EQUAL(rc, 0);
    check_out("testproc");
}
