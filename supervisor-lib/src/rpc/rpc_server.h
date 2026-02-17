// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__RPC__RPC_SERVER
#define SUPERVISOR_LIB__RPC__RPC_SERVER

#include "rpc_fwd.h"
#include <functional>
#include <map>
#include <boost/asio.hpp>

namespace supervisorcpp::rpc {

/**
 * RPC method handler type
 * Takes parameters as vector of strings, returns result as string
 */
using RpcHandler = std::function<std::string(const RpcParams&)>;

/**
 * Generic XML-RPC server over Unix domain sockets.
 * Accepts connections, parses XML-RPC requests, and dispatches
 * to registered handlers. Has no knowledge of what the handlers do.
 */
class RpcServer : public std::enable_shared_from_this<RpcServer> {
    struct UseCreate { };
public:
    static auto create(boost::asio::io_context& io_context,
                       const std::string& socket_path) {
        return std::make_shared<RpcServer>(UseCreate{}, io_context, socket_path);
    }

    RpcServer(UseCreate, boost::asio::io_context& io_context,
              const std::string& socket_path);
    ~RpcServer();

    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;
    RpcServer(RpcServer&&) = delete;
    RpcServer& operator=(RpcServer&&) = delete;

    void register_handler(const std::string& name, RpcHandler handler);
    std::string dispatch_method(const std::string& method_name, const RpcParams& params);

    void start();
    void stop();

private:
    void start_accept_();
    void handle_accept_(const RpcConnectionPtr& connection_ptr, const boost::system::error_code& error);

    boost::asio::io_context& io_context_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
    std::string socket_path_;
    std::map<std::string, RpcHandler> handlers_;
    bool running_{false};
};

} // namespace supervisorcpp::rpc

#endif // SUPERVISOR_LIB__RPC__RPC_SERVER
