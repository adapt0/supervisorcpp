#pragma once

#include "../process/process_manager.h"
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <map>

namespace supervisord {
namespace rpc {

// Forward declarations
class RpcConnection;

/**
 * RPC method handler type
 * Takes parameters as vector of strings, returns result as string
 */
using RpcHandler = std::function<std::string(const std::vector<std::string>&)>;

/**
 * XML-RPC server for supervisord
 * Listens on Unix domain socket and handles supervisorctl requests
 */
class RpcServer {
public:
    RpcServer(boost::asio::io_context& io_context,
              const std::string& socket_path,
              process::ProcessManager& process_manager);
    ~RpcServer();

    // Disable copy and move
    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;
    RpcServer(RpcServer&&) = delete;
    RpcServer& operator=(RpcServer&&) = delete;

    /**
     * Start accepting connections
     */
    void start();

    /**
     * Stop the server
     */
    void stop();

private:
    /**
     * Start accepting new connection
     */
    void start_accept();

    /**
     * Handle new connection
     */
    void handle_accept(std::shared_ptr<RpcConnection> connection,
                      const boost::system::error_code& error);

    /**
     * Register RPC method handlers
     */
    void register_handlers();

    /**
     * Dispatch RPC method call
     */
    std::string dispatch_method(const std::string& method_name,
                               const std::vector<std::string>& params);

    // RPC method handlers
    std::string handle_get_state(const std::vector<std::string>& params);
    std::string handle_get_all_process_info(const std::vector<std::string>& params);
    std::string handle_get_process_info(const std::vector<std::string>& params);
    std::string handle_start_process(const std::vector<std::string>& params);
    std::string handle_stop_process(const std::vector<std::string>& params);
    std::string handle_start_all_processes(const std::vector<std::string>& params);
    std::string handle_stop_all_processes(const std::vector<std::string>& params);
    std::string handle_restart_process(const std::vector<std::string>& params);
    std::string handle_shutdown(const std::vector<std::string>& params);

    // IO context and acceptor
    boost::asio::io_context& io_context_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
    std::string socket_path_;

    // Process manager reference
    process::ProcessManager& process_manager_;

    // RPC method handlers
    std::map<std::string, RpcHandler> handlers_;

    // Flag for shutdown
    bool running_;

    friend class RpcConnection;
};

/**
 * Single RPC connection handler
 */
class RpcConnection : public std::enable_shared_from_this<RpcConnection> {
public:
    RpcConnection(boost::asio::io_context& io_context, RpcServer& server);

    boost::asio::local::stream_protocol::socket& socket() { return socket_; }

    /**
     * Start reading request
     */
    void start();

private:
    /**
     * Handle read completion
     */
    void handle_read(const boost::system::error_code& error, size_t bytes_transferred);

    /**
     * Handle write completion
     */
    void handle_write(const boost::system::error_code& error);

    /**
     * Process HTTP request
     */
    void process_request(const std::string& request);

    /**
     * Parse XML-RPC request
     * Returns method name and parameters
     */
    std::pair<std::string, std::vector<std::string>> parse_xmlrpc_request(const std::string& xml);

    /**
     * Generate XML-RPC response
     */
    std::string generate_xmlrpc_response(const std::string& result);

    /**
     * Generate XML-RPC fault response
     */
    std::string generate_xmlrpc_fault(int code, const std::string& message);

    /**
     * Generate HTTP response
     */
    std::string generate_http_response(const std::string& body);

    boost::asio::local::stream_protocol::socket socket_;
    RpcServer& server_;
    std::array<char, 8192> buffer_;
    std::string request_data_;
};

} // namespace rpc
} // namespace supervisord
