#include "rpc_server.h"
#include "rpc_connection.h"
#include "../logger/logger.h"
#include "../util/secure.h"

namespace supervisorcpp::rpc {

RpcServer::RpcServer(UseCreate, boost::asio::io_context& io_context,
                     const std::string& socket_path)
: io_context_(io_context)
, acceptor_(io_context)
, socket_path_(socket_path)
, running_(false)
{ }

RpcServer::~RpcServer() {
    stop();
}

void RpcServer::register_handler(const std::string& name, RpcHandler handler) {
    handlers_[name] = std::move(handler);
}

std::string RpcServer::dispatch_method(const std::string& method_name, const RpcParams& params) {
    if (const auto it = handlers_.find(method_name); std::end(handlers_) != it) {
        return it->second(params);
    } else {
        throw std::runtime_error("Unknown method: " + method_name);
    }
}

void RpcServer::start() {
    // SECURITY: Validate socket directory before creating socket
    util::validate_socket_directory(socket_path_);

    // Remove existing socket file if it exists
    ::unlink(socket_path_.c_str());

    // Create endpoint
    boost::asio::local::stream_protocol::endpoint endpoint(socket_path_);

    // Open and bind acceptor
    acceptor_.open(endpoint.protocol());
    acceptor_.bind(endpoint);

    // SECURITY: Set socket permissions to 0600 (owner read/write only)
    // Must happen BEFORE listen() to avoid TOCTOU race where the socket
    // is briefly connectable with default permissions.
    try {
        util::set_socket_permissions(socket_path_);
    } catch (const util::SecurityError& e) {
        LOG_ERROR << "Failed to set socket permissions: " << e.what();
        throw;
    }

    acceptor_.listen();
    running_ = true;
    LOG_INFO << "RPC server listening on " << socket_path_;

    start_accept_();
}

void RpcServer::stop() {
    if (!running_) return;

    running_ = false;
    acceptor_.close();
    ::unlink(socket_path_.c_str());

    LOG_INFO << "RPC server stopped";
}

void RpcServer::start_accept_() {
    const auto connection_ptr = std::make_shared<RpcConnection>(io_context_, [
        self_weak = RpcServerWeak{shared_from_this()}
    ](const std::string& method, const RpcParams& params) {
        const auto self_ptr = self_weak.lock();
        if (!self_ptr) throw std::runtime_error("Server is shutting down");
        return self_ptr->dispatch_method(method, params);
    });

    acceptor_.async_accept(connection_ptr->socket(), [
        this, connection_ptr
    ](const boost::system::error_code& error) {
        handle_accept_(connection_ptr, error);
    });
}

void RpcServer::handle_accept_(const RpcConnectionPtr& connection_ptr,
                              const boost::system::error_code& error) {
    if (!error) {
        LOG_DEBUG << "RPC connection accepted";
        connection_ptr->start();
    } else {
        LOG_ERROR << "RPC accept error: " << error.message();
    }

    if (running_) start_accept_();
}

} // namespace supervisorcpp::rpc
