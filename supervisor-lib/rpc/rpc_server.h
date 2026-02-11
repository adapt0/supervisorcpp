#pragma once
#ifndef SUPERVISOR_LIB__RPC__RPC_SERVER
#define SUPERVISOR_LIB__RPC__RPC_SERVER

#include "rpc_fwd.h"
#include <functional>
#include <map>
#include <boost/asio.hpp>

namespace supervisorcpp::process {
    class ProcessManager;
}

namespace supervisorcpp::rpc {

/**
 * RPC method handler type
 * Takes parameters as vector of strings, returns result as string
 */
using RpcHandler = std::function<std::string(const RpcParams&)>;

/**
 * XML-RPC server for supervisord
 * Listens on Unix domain socket and handles supervisorctl requests
 */
class RpcServer : public std::enable_shared_from_this<RpcServer> {
    struct UseCreate { };
public:
    static auto create(boost::asio::io_context& io_context,
                       const std::string& socket_path,
                       process::ProcessManager& process_manager
    ) {
        return std::make_shared<RpcServer>(UseCreate{}, io_context, socket_path, process_manager);
    }

    RpcServer(UseCreate, boost::asio::io_context& io_context,
              const std::string& socket_path,
              process::ProcessManager& process_manager);
    ~RpcServer();

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

    /**
     * Dispatch RPC method call
     */
    std::string dispatch_method(const std::string& method_name, const RpcParams& params);

private:
    /**
     * Start accepting new connection
     */
    void start_accept_();

    /**
     * Handle new connection
     */
    void handle_accept_(const RpcConnectionPtr& connection_ptr, const boost::system::error_code& error);

    /**
     * Register RPC method handlers
     */
    void register_handlers_();

    // RPC method handlers
    std::string handle_get_state_(const RpcParams& params);
    std::string handle_get_all_process_info_(const RpcParams& params);
    std::string handle_get_process_info_(const RpcParams& params);
    std::string handle_start_process_(const RpcParams& params);
    std::string handle_stop_process_(const RpcParams& params);
    std::string handle_start_all_processes_(const RpcParams& params);
    std::string handle_stop_all_processes_(const RpcParams& params);
    std::string handle_restart_process_(const RpcParams& params);
    std::string handle_shutdown_(const RpcParams& params);

    // IO context and acceptor
    boost::asio::io_context& io_context_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
    std::string socket_path_;

    // Process manager reference
    process::ProcessManager& process_manager_;

    // RPC method handlers
    std::map<std::string, RpcHandler> handlers_;

    bool running_{false};   ///< shutdown flag
};

} // namespace supervisorcpp::rpc

#endif // SUPERVISOR_LIB__RPC__RPC_SERVER
