#define BOOST_TEST_MODULE RpcDispatchTest
#include <boost/test/unit_test.hpp>
#include "rpc/rpc_server.h"
#include "util/test_util.h"
#include <boost/asio.hpp>

namespace rpc = supervisorcpp::rpc;
using TempManager = test_util::TempManager;

struct RpcFixture {
    TempManager::Cleanup sock = TempManager::file("rpc_dispatch.sock");
    boost::asio::io_context io;
    rpc::RpcServerPtr server = rpc::RpcServer::create(io, sock.str());
};

BOOST_FIXTURE_TEST_CASE(rpc_dispatch__routes_to_handler, RpcFixture) {
    size_t called = 0;
    server->register_handler("test.method", [&called](const rpc::RpcParams&) {
        called++;
        return "<string>ok</string>";
    });
    BOOST_CHECK_EQUAL(called, 0);
    {
        const auto result = server->dispatch_method("test.method", {});
        BOOST_CHECK_EQUAL(called, 1);
        BOOST_CHECK_EQUAL(result, "<string>ok</string>");
    }

    // last registration wins
    server->register_handler("test.method", [&called](const rpc::RpcParams&) {
        called += 3;
        return "<string>second</string>";
    });
    {
        const auto result = server->dispatch_method("test.method", {});
        BOOST_CHECK_EQUAL(called, 4);
        BOOST_CHECK_EQUAL(result, "<string>second</string>");
    }
}

BOOST_FIXTURE_TEST_CASE(rpc_dispatch__unknown_method_throws, RpcFixture) {
    BOOST_CHECK_EXCEPTION(
        server->dispatch_method("no.such.method", {}),
        std::runtime_error,
        [](const std::runtime_error& e) {
            return std::string_view{e.what()}.find("no.such.method") != std::string_view::npos;
        });
}

BOOST_FIXTURE_TEST_CASE(rpc_dispatch__passes_params, RpcFixture) {
    rpc::RpcParams received;
    server->register_handler("test.echo", [&received](const rpc::RpcParams& p) {
        received = p;
        return "<string>ok</string>";
    });

    server->dispatch_method("test.echo", {"arg1", "arg2"});
    BOOST_REQUIRE_EQUAL(received.size(), 2);
    BOOST_CHECK_EQUAL(received[0], "arg1");
    BOOST_CHECK_EQUAL(received[1], "arg2");
}

BOOST_FIXTURE_TEST_CASE(rpc_dispatch__handler_exception_propagates, RpcFixture) {
    server->register_handler("test.fail", [](const rpc::RpcParams&) -> std::string {
        throw std::runtime_error("handler error");
    });

    BOOST_CHECK_EXCEPTION(
        server->dispatch_method("test.fail", {}),
        std::runtime_error,
        [](const std::runtime_error& e) {
            return std::string_view{e.what()} == "handler error";
        });
}
