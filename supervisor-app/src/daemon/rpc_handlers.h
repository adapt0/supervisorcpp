#pragma once
#ifndef SUPERVISOR_APP__RPC_HANDLERS
#define SUPERVISOR_APP__RPC_HANDLERS

#include "process/process_manager.h"
#include "rpc/rpc_server.h"
#include "rpc/xmlrpc.h"

namespace supervisorcpp {

/**
 * Register the standard process-management RPC handlers.
 * Shared between supervisord and tests.
 */
inline void register_process_handlers(rpc::RpcServer& server, process::ProcessManager& pm) {
    using rpc::RpcParams;
    using namespace xmlrpc;

    server.register_handler("supervisor.getAllProcessInfo", [&pm](const RpcParams&) {
        return wrap(pm.get_all_process_info()).str();
    });

    server.register_handler("supervisor.getProcessInfo", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto* proc = pm.get_process(params[0]);
        if (!proc) throw std::runtime_error("BAD_NAME: " + params[0]);
        return wrap(proc->get_info()).str();
    });

    server.register_handler("supervisor.startProcess", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto& name = params[0];
        const auto* proc = pm.get_process(name);
        if (!proc) throw std::runtime_error("BAD_NAME: " + name);
        const auto st = proc->state();
        if (st == process::State::RUNNING || st == process::State::STARTING) {
            throw std::runtime_error("ALREADY_STARTED: " + name);
        }
        if (!pm.start_process(name)) {
            throw std::runtime_error("SPAWN_ERROR: " + name);
        }
        return Value{true}.str();
    });

    server.register_handler("supervisor.stopProcess", [&pm](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto& name = params[0];
        const auto* proc = pm.get_process(name);
        if (!proc) throw std::runtime_error("BAD_NAME: " + name);
        const auto st = proc->state();
        if (st == process::State::STOPPED || st == process::State::EXITED || st == process::State::FATAL) {
            throw std::runtime_error("NOT_RUNNING: " + name);
        }
        if (!pm.stop_process(name)) {
            throw std::runtime_error("NOT_RUNNING: " + name);
        }
        return Value{true}.str();
    });

    server.register_handler("supervisor.startAllProcesses", [&pm](const RpcParams&) {
        pm.start_all();
        return wrap(pm.get_all_process_info()).str();
    });

    server.register_handler("supervisor.stopAllProcesses", [&pm](const RpcParams&) {
        pm.stop_all();
        return wrap(pm.get_all_process_info()).str();
    });
}

} // namespace supervisorcpp

#endif // SUPERVISOR_APP__RPC_HANDLERS
