#pragma once
#ifndef SUPERVISOR_APP__RPC_HANDLERS
#define SUPERVISOR_APP__RPC_HANDLERS

#include "rpc/rpc_server.h"
#include "rpc/xmlrpc.h"
#include "process/process_manager.h"
#include <boost/asio.hpp>
#include <sstream>

namespace supervisorcpp::rpc {

inline void register_supervisor_handlers(
    RpcServer& server,
    process::ProcessManager& pm,
    boost::asio::io_context& io)
{
    server.register_handler("supervisor.getState", [](const RpcParams&) {
        return xmlrpc::Struct{
            xmlrpc::Member{"statecode", 1},
            xmlrpc::Member{"statename", "RUNNING"},
        }.str();
    });

    server.register_handler("supervisor.getAllProcessInfo", [&pm](const RpcParams&) {
        return xmlrpc::wrap( pm.get_all_process_info() ).str();
    });

    server.register_handler("supervisor.getProcessInfo", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto* proc = pm.get_process(params[0]);
        if (!proc) throw std::runtime_error("Process not found: " + params[0]);

        return xmlrpc::wrap( proc->get_info() ).str();
    });

    server.register_handler("supervisor.startProcess", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        return xmlrpc::Value{pm.start_process(params[0])}.str();
    });

    server.register_handler("supervisor.stopProcess", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        return xmlrpc::Value{pm.stop_process(params[0])}.str();
    });

    server.register_handler("supervisor.startAllProcesses", [&pm](const RpcParams&) {
        pm.start_all();
        return xmlrpc::wrap( pm.get_all_process_info() ).str();
    });

    server.register_handler("supervisor.stopAllProcesses", [&pm](const RpcParams&) {
        pm.stop_all();
        return xmlrpc::wrap( pm.get_all_process_info() ).str();
    });

    server.register_handler("supervisor.restart", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        return xmlrpc::Value{ pm.restart_process(params[0]) }.str();
    });

    server.register_handler("supervisor.shutdown", [&pm, &io](const RpcParams&) {
        boost::asio::post(io, [&pm, &io]() {
            pm.stop_all();
            io.stop();
        });
        return xmlrpc::Value{true}.str();
    });

    // Compatibility stubs for Python supervisorctl handshake
    const auto api_version = [](const RpcParams&) -> std::string {
        return "<string>3.0</string>";
    };
    server.register_handler("supervisor.getVersion", api_version);
    server.register_handler("supervisor.getSupervisorVersion", api_version);
    server.register_handler("supervisor.getAPIVersion", api_version);

    server.register_handler("supervisor.getIdentification", [](const RpcParams&) -> std::string {
        return "<string>supervisor</string>";
    });

    server.register_handler("supervisor.getPID", [](const RpcParams&) -> std::string {
        return "<int>" + std::to_string(getpid()) + "</int>";
    });
}

} // namespace supervisorcpp::rpc

#endif // SUPERVISOR_APP__RPC_HANDLERS
