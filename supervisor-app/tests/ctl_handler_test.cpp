/**
 * Integration tests for supervisorctl client interactions.
 * Tests the full XML-RPC roundtrip over a real Unix socket:
 *   client serialization → socket → server dispatch → fault response → client parsing
 */

#define BOOST_TEST_MODULE CtlHandlerTest
#include <boost/test/unit_test.hpp>
#include "ctl/supervisorctl.h"
#include "daemon/rpc_handlers.h"
#include "process/process_manager.h"
#include "rpc/rpc_server.h"
#include "util/test_util.h"
#include <thread>
#include <boost/asio.hpp>

namespace rpc = supervisorcpp::rpc;
namespace process = supervisorcpp::process;
using namespace std::chrono_literals;
using TempManager = test_util::TempManager;
using SupervisorCtlClient = supervisorcpp::SupervisorCtlClient;

struct CtlFixture {
    CtlFixture() {
        supervisorcpp::config::ProgramConfig cfg;
        cfg.name = "testproc";
        cfg.command = "/bin/sleep 60";
        cfg.autorestart = false;
        cfg.startsecs = 1;
        cfg.stdout_log.file = log2_.path();
        pm_.add_process(cfg);

        supervisorcpp::register_process_handlers(*server_, pm_);
        server_->start();

        // Run io_context in background so server can handle connections
        server_thread_ = std::jthread([this](std::stop_token) {
            const auto work = boost::asio::make_work_guard(io_);
            io_.run();
        });
    }

    ~CtlFixture() {
        pm_.stop_all();
        server_->stop();
        io_.stop();
        if (server_thread_.joinable()) server_thread_.join();
    }

    SupervisorCtlClient make_client() {
        out_.str("");
        err_.str("");
        return SupervisorCtlClient{sock_.str(), out_, err_};
    }

    void check_out_contains(std::string_view expected) {
        BOOST_CHECK_MESSAGE(out_.str().contains(expected),
            "expected '" << expected << "' in stdout: \"" << out_.str() << "\"");
    }
    void check_err_contains(std::string_view expected) {
        BOOST_CHECK_MESSAGE(err_.str().contains(expected),
            "expected '" << expected << "' in stderr: \"" << err_.str() << "\"");
    }
    auto err_str() const { return err_.str(); }

    /// Sleep-poll until condition is met or timeout (does not touch io_context)
    static bool poll_until(std::function<bool()> condition, std::chrono::milliseconds timeout = 5000ms) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (condition()) return true;
            std::this_thread::sleep_for(50ms);
        }
        return false;
    }

    void wait_for(process::State state) {
        poll_until([this, state] {
            const auto info = pm_.get_all_process_info();
            return !info.empty() && info[0].state == state;
        });
    }

    auto& pm() noexcept { return pm_; }

private:
    TempManager::Cleanup sock_ = TempManager::file("ctl_test.sock");
    TempManager::Cleanup log1_ = TempManager::file("ctl_test_d.log");
    TempManager::Cleanup log2_ = TempManager::file("ctl_test_p.log");

    boost::asio::io_context io_;
    process::ProcessManager pm_{io_, 100ms};
    rpc::RpcServerPtr server_ = rpc::RpcServer::create(io_, sock_.str());

    std::ostringstream out_;
    std::ostringstream err_;

    std::jthread server_thread_;
};


// --- BAD_NAME tests (non-existent process) ---

BOOST_FIXTURE_TEST_CASE(ctl__start_bad_name, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("start", {"nonexistent"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err_contains("no such process");
}

BOOST_FIXTURE_TEST_CASE(ctl__stop_bad_name, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("stop", {"nonexistent"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err_contains("no such process");
}

BOOST_FIXTURE_TEST_CASE(ctl__status_bad_name, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("status", {"nonexistent"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err_contains("no such process");
}

// --- Edge case: stop a stopped process ---

BOOST_FIXTURE_TEST_CASE(ctl__stop_stopped_process, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("stop", {"testproc"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err_contains("not running");
}

// --- Edge case: start an already-running process ---

BOOST_FIXTURE_TEST_CASE(ctl__start_already_running, CtlFixture) {
    pm().start_process("testproc");
    wait_for(process::State::RUNNING);

    auto client = make_client();
    const auto rc = client.execute_command("start", {"testproc"});
    BOOST_CHECK_EQUAL(rc, 1);
    check_err_contains("already running");
}

// --- Restart of a stopped process (tolerates NOT_RUNNING on stop) ---

BOOST_FIXTURE_TEST_CASE(ctl__restart_stopped_process, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("restart", {"testproc"});
    BOOST_CHECK_EQUAL(err_str(), "");
    BOOST_CHECK_EQUAL(rc, 0);
    check_out_contains("restarted");
}

// --- Normal start and stop ---

BOOST_FIXTURE_TEST_CASE(ctl__start_and_stop, CtlFixture) {
    {
        auto client = make_client();
        const auto rc = client.execute_command("start", {"testproc"});
        BOOST_CHECK_EQUAL(err_str(), "");
        BOOST_CHECK_EQUAL(rc, 0);
        check_out_contains("started");
    }

    wait_for(process::State::RUNNING);

    {
        auto client = make_client();
        const auto rc = client.execute_command("stop", {"testproc"});
        BOOST_CHECK_EQUAL(err_str(), "");
        BOOST_CHECK_EQUAL(rc, 0);
        check_out_contains("stopped");
    }
}

// --- Status all ---

BOOST_FIXTURE_TEST_CASE(ctl__status_all, CtlFixture) {
    auto client = make_client();
    const auto rc = client.execute_command("status", {});
    BOOST_CHECK_EQUAL(err_str(), "");
    BOOST_CHECK_EQUAL(rc, 0);
    check_out_contains("testproc");
}
