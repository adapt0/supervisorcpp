#define BOOST_TEST_MODULE RpcConnectionTest
#include <boost/test/unit_test.hpp>
#include "rpc/rpc_connection.h"
#include "rpc/rpc_server.h"
#include "../test_util.h"
#include <boost/asio.hpp>

namespace rpc = supervisorcpp::rpc;
using TempManager = test_util::TempManager;

// Helper: build an HTTP POST request wrapping an XML-RPC body
static std::string make_http_request(const std::string& xml_body) {
    std::string req;
    req += "POST /RPC2 HTTP/1.1\r\n";
    req += "Content-Type: text/xml\r\n";
    req += "Content-Length: " + std::to_string(xml_body.size()) + "\r\n";
    req += "Connection: close\r\n";
    req += "\r\n";
    req += xml_body;
    return req;
}

// Helper: build an XML-RPC methodCall
static std::string make_xmlrpc_call(const std::string& method,
                                     const std::vector<std::string>& params = {}) {
    std::string xml;
    xml += "<?xml version=\"1.0\"?>\n";
    xml += "<methodCall>\n";
    xml += "  <methodName>" + method + "</methodName>\n";
    xml += "  <params>\n";
    for (const auto& p : params) {
        xml += "    <param><value><string>" + p + "</string></value></param>\n";
    }
    xml += "  </params>\n";
    xml += "</methodCall>\n";
    return xml;
}

// Helper: send request on one end, run io_context, read response from same end
static std::string send_and_receive(boost::asio::io_context& io,
                                     boost::asio::local::stream_protocol::socket& client_socket,
                                     const std::string& request) {
    // Write the request
    boost::asio::write(client_socket, boost::asio::buffer(request));

    // Shutdown write side so the connection sees EOF for header detection
    client_socket.shutdown(boost::asio::local::stream_protocol::socket::shutdown_send);

    // Run the io_context to process the request and send response
    io.run();
    io.restart();

    // Read the response
    boost::asio::streambuf response_buf;
    boost::system::error_code ec;
    boost::asio::read(client_socket, response_buf, ec);
    // EOF is expected after response
    return std::string(
        boost::asio::buffers_begin(response_buf.data()),
        boost::asio::buffers_end(response_buf.data()));
}

struct ConnectionFixture {
    boost::asio::io_context io;
    boost::asio::local::stream_protocol::socket client{io};

    ConnectionFixture() {
        boost::asio::local::stream_protocol::socket server_end{io};
        boost::asio::local::connect_pair(client, server_end);
        server_end_ = std::move(server_end);
    }

    void start(rpc::DispatchFunction dispatcher) {
        auto connection = std::make_shared<rpc::RpcConnection>(io, std::move(dispatcher));
        connection->socket() = std::move(server_end_);
        connection->start();
    }

    std::string call(const std::string& method, const std::vector<std::string>& params = {}) {
        return send_and_receive(io, client,
            make_http_request(make_xmlrpc_call(method, params)));
    }

    std::string send_raw(const std::string& body) {
        return send_and_receive(io, client, make_http_request(body));
    }

private:
    boost::asio::local::stream_protocol::socket server_end_{io};
};

BOOST_FIXTURE_TEST_CASE(rpc_connection__happy_path, ConnectionFixture) {
    std::string received_method;
    rpc::RpcParams received_params;
    start([&](const std::string& method, const rpc::RpcParams& params) {
        received_method = method;
        received_params = params;
        return "<string>ok</string>";
    });

    auto response = call("supervisor.getState");

    BOOST_CHECK_EQUAL(received_method, "supervisor.getState");
    BOOST_CHECK(received_params.empty());
    BOOST_CHECK(response.find("HTTP/1.1 200 OK") != std::string::npos);
    BOOST_CHECK(response.find("<methodResponse>") != std::string::npos);
    BOOST_CHECK(response.find("<string>ok</string>") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(rpc_connection__with_params, ConnectionFixture) {
    rpc::RpcParams received_params;
    start([&](const std::string&, const rpc::RpcParams& params) {
        received_params = params;
        return "<boolean>1</boolean>";
    });

    auto response = call("supervisor.startProcess", {"myapp"});

    BOOST_REQUIRE_EQUAL(received_params.size(), 1);
    BOOST_CHECK_EQUAL(received_params[0], "myapp");
    BOOST_CHECK(response.find("<methodResponse>") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(rpc_connection__malformed_xml, ConnectionFixture) {
    start([](const std::string&, const rpc::RpcParams&) -> std::string {
        BOOST_FAIL("Dispatcher should not be called for malformed XML");
        return "";
    });

    auto response = send_raw("this is not valid xml <><><");

    BOOST_CHECK(response.find("<fault>") != std::string::npos);
    BOOST_CHECK(response.find("faultString") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(rpc_connection__dispatch_exception, ConnectionFixture) {
    start([](const std::string&, const rpc::RpcParams&) -> std::string {
        throw std::runtime_error("test error from dispatcher");
    });

    auto response = call("supervisor.getState");

    BOOST_CHECK(response.find("<fault>") != std::string::npos);
    BOOST_CHECK(response.find("test error from dispatcher") != std::string::npos);
}

BOOST_FIXTURE_TEST_CASE(rpc_connection__request_size_limit, ConnectionFixture) {
    start([](const std::string&, const rpc::RpcParams&) -> std::string {
        BOOST_FAIL("Dispatcher should not be called for oversized request");
        return "";
    });

    // Send raw data WITHOUT \r\n\r\n so the connection keeps reading
    // until it exceeds the 1MB limit. Uses async_write because a synchronous
    // write of >1MB would block (socket buffer fills before io.run() starts
    // the server-side read loop).
    auto huge_data = std::make_shared<std::string>(1024 * 1024 + 100, 'X');
    boost::asio::async_write(client, boost::asio::buffer(*huge_data),
        [huge_data](const boost::system::error_code&, size_t) {});

    io.run();
    io.restart();

    boost::asio::streambuf response_buf;
    boost::system::error_code ec;
    boost::asio::read(client, response_buf, ec);
    auto response = std::string(
        boost::asio::buffers_begin(response_buf.data()),
        boost::asio::buffers_end(response_buf.data()));

    BOOST_CHECK(response.find("<fault>") != std::string::npos);
    BOOST_CHECK(response.find("Request too large") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rpc_connection__unix_socket_accept) {
    auto sock_file = TempManager::file("rpc_accept.sock");

    boost::asio::io_context io;
    auto server = rpc::RpcServer::create(io, sock_file.str());
    server->register_handler("supervisor.getState", [](const rpc::RpcParams&) {
        return "<string>test-ok</string>";
    });
    server->start();

    // Connect and send request before io.run() — kernel buffers everything
    boost::asio::local::stream_protocol::socket client(io);
    client.connect(boost::asio::local::stream_protocol::endpoint(sock_file.str()));

    auto request = make_http_request(make_xmlrpc_call("supervisor.getState"));
    boost::asio::write(client, boost::asio::buffer(request));
    client.shutdown(boost::asio::local::stream_protocol::socket::shutdown_send);

    // Async read response; stop server + io when done so io.run() exits
    std::string response;
    boost::asio::streambuf response_buf;
    boost::asio::async_read(client, response_buf,
        [&](const boost::system::error_code&, size_t) {
            response = std::string(
                boost::asio::buffers_begin(response_buf.data()),
                boost::asio::buffers_end(response_buf.data()));
            server->stop();
            io.stop();
        });

    io.run();

    BOOST_CHECK(response.find("HTTP/1.1 200 OK") != std::string::npos);
    BOOST_CHECK(response.find("<methodResponse>") != std::string::npos);
    BOOST_CHECK(response.find("test-ok") != std::string::npos);
}
