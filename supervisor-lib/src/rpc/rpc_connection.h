// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__RPC__RPC_CONNECTION
#define SUPERVISOR_LIB__RPC__RPC_CONNECTION

#include "rpc_fwd.h"
#include <functional>
#include <boost/asio.hpp>

namespace supervisorcpp::rpc {

/**
 * Dispatch callback: takes (method_name, params) and returns result string
 */
using DispatchFunction = std::function<std::string(const std::string&, const RpcParams&)>;

/**
 * Single RPC connection handler
 */
class RpcConnection : public std::enable_shared_from_this<RpcConnection> {
public:
    RpcConnection(boost::asio::io_context& io_context, DispatchFunction dispatcher);
    ~RpcConnection();

    boost::asio::local::stream_protocol::socket& socket() { return socket_; }

    /**
     * Start reading request
     */
    void start();

private:
    /**
     * begin reading next chunk
     */
    void begin_read_();

    /**
     * Handle read completion
     */
    void handle_read_(const boost::system::error_code& error, size_t bytes_transferred);

    /**
     * Send HTTP response with owned buffer lifetime
     * Wraps data in shared_ptr so async_write outlives the caller's scope
     */
    void send_response_(const std::string& body);

    /**
     * Handle write completion
     */
    void handle_write_(const boost::system::error_code& error);

    /**
     * Process HTTP request
     */
    void process_request_(const std::string& request);

    /**
     * Parse XML-RPC request
     * Returns method name and parameters
     */
    std::pair<std::string, RpcParams> parse_xmlrpc_request_(std::string&& xml);

    /**
     * Generate XML-RPC response
     */
    std::string generate_xmlrpc_response_(const std::string& result);

    /**
     * Generate XML-RPC fault response
     */
    std::string generate_xmlrpc_fault_(int code, const std::string& message);

    /**
     * Generate HTTP response
     */
    std::string generate_http_response_(const std::string& body);

    boost::asio::local::stream_protocol::socket socket_;
    DispatchFunction dispatcher_;
    std::array<char, 8192> buffer_;
    std::string request_data_;
};

} // namespace supervisorcpp::rpc

#endif // SUPERVISOR_LIB__RPC__RPC_CONNECTION
