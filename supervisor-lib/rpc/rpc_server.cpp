#include "rpc_server.h"
#include "rpc_connection.h"
#include "xmlrpc.h"
#include "../process/process_manager.h"
#include "../logger/logger.h"
#include "../util/secure.h"

namespace supervisorcpp::rpc {

// ============================================================================
// RpcServer implementation
// ============================================================================

RpcServer::RpcServer(UseCreate, boost::asio::io_context& io_context,
                     const std::string& socket_path,
                     process::ProcessManager& process_manager)
: io_context_(io_context)
, acceptor_(io_context)
, socket_path_(socket_path)
, process_manager_(process_manager)
, running_(false)
{
    register_handlers_();
}

RpcServer::~RpcServer() {
    stop();
}

void RpcServer::start() {
    // Remove existing socket file if it exists
    ::unlink(socket_path_.c_str());

    // Create endpoint
    boost::asio::local::stream_protocol::endpoint endpoint(socket_path_);

    // Open and bind acceptor
    acceptor_.open(endpoint.protocol());
    acceptor_.bind(endpoint);
    acceptor_.listen();

    // SECURITY: Set socket permissions to 0600 (owner read/write only)
    try {
        util::set_socket_permissions(socket_path_);
    } catch (const util::SecurityError& e) {
        LOG_ERROR << "Failed to set socket permissions: " << e.what();
        throw;
    }

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

void RpcServer::register_handlers_() {
    handlers_["supervisor.getState"] = [this](auto& p) { return handle_get_state_(p); };
    handlers_["supervisor.getAllProcessInfo"] = [this](auto& p) { return handle_get_all_process_info_(p); };
    handlers_["supervisor.getProcessInfo"] = [this](auto& p) { return handle_get_process_info_(p); };
    handlers_["supervisor.startProcess"] = [this](auto& p) { return handle_start_process_(p); };
    handlers_["supervisor.stopProcess"] = [this](auto& p) { return handle_stop_process_(p); };
    handlers_["supervisor.startAllProcesses"] = [this](auto& p) { return handle_start_all_processes_(p); };
    handlers_["supervisor.stopAllProcesses"] = [this](auto& p) { return handle_stop_all_processes_(p); };
    handlers_["supervisor.restart"] = [this](auto& p) { return handle_restart_process_(p); };
    handlers_["supervisor.shutdown"] = [this](auto& p) { return handle_shutdown_(p); };

    // Compatibility stubs for Python supervisorctl handshake
    const auto apiVersion = [](const RpcParams&) -> std::string {
        return "<string>3.0</string>";
    };
    handlers_["supervisor.getVersion"] = apiVersion;
    handlers_["supervisor.getSupervisorVersion"] = apiVersion;
    handlers_["supervisor.getAPIVersion"] = apiVersion;
    handlers_["supervisor.getIdentification"] = [](auto&) -> std::string {
        return "<string>supervisor</string>";
    };
    handlers_["supervisor.getPID"] = [](auto&) -> std::string {
        return "<int>" + std::to_string(getpid()) + "</int>";
    };
}

std::string RpcServer::dispatch_method(const std::string& method_name, const RpcParams& params) {
    if (const auto it = handlers_.find(method_name); std::end(handlers_) != it) {
        return it->second(params);
    } else {
        throw std::runtime_error("Unknown method: " + method_name);
    }
}

std::string RpcServer::handle_get_state_(const RpcParams& /*params*/) {
    // Return supervisor state
    // States: RUNNING=1, others not implemented in minimal version
    std::ostringstream oss;
    oss << "<struct>"
        << xmlrpc::Member{"statecode", 1}
        << xmlrpc::Member{"statename", "RUNNING"}
        << "</struct>"
    ;
    return oss.str();
}

std::string RpcServer::handle_get_all_process_info_(const RpcParams& /*params*/) {
    auto infos = process_manager_.get_all_process_info();

    std::ostringstream oss;
    oss << "<array><data>";
    for (const auto& info : infos) {
        oss << "<value>" << xmlrpc::XmlProcessInfo{info} << "</value>";
    }
    oss << "</data></array>";
    return oss.str();
}

std::string RpcServer::handle_get_process_info_(const RpcParams& params) {
    if (params.empty()) throw std::runtime_error("Process name required");

    const auto* proc = process_manager_.get_process(params[0]);
    if (!proc) throw std::runtime_error("Process not found: " + params[0]);

    const auto info = proc->get_info();
    return (std::ostringstream{} << xmlrpc::XmlProcessInfo{info}).str();
}

std::string RpcServer::handle_start_process_(const RpcParams& params) {
    if (params.empty()) throw std::runtime_error("Process name required");

    const bool result = process_manager_.start_process(params[0]);
    return xmlrpc::Value{result}.toString();
}

std::string RpcServer::handle_stop_process_(const RpcParams& params) {
    if (params.empty()) throw std::runtime_error("Process name required");

    const bool result = process_manager_.stop_process(params[0]);
    return xmlrpc::Value{result}.toString();
}

std::string RpcServer::handle_start_all_processes_(const RpcParams& /*params*/) {
    process_manager_.start_all();

    // Return array of process infos
    return handle_get_all_process_info_({});
}

std::string RpcServer::handle_stop_all_processes_(const RpcParams& /*params*/) {
    process_manager_.stop_all();

    // Return array of process infos
    return handle_get_all_process_info_({});
}

std::string RpcServer::handle_restart_process_(const RpcParams& params) {
    if (params.empty()) throw std::runtime_error("Process name required");

    const bool result = process_manager_.restart_process(params[0]);
    return xmlrpc::Value{result}.toString();
}

std::string RpcServer::handle_shutdown_(const RpcParams& /*params*/) {
    LOG_INFO << "Shutdown requested via RPC";

    // Stop the IO context to trigger shutdown
    boost::asio::post(io_context_, [this]() {
        process_manager_.stop_all();
        io_context_.stop();
    });

    return xmlrpc::Value{true}.toString();
}

} // namespace supervisorcpp::rpc
