// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_APP__SUPERVISORD
#define SUPERVISOR_APP__SUPERVISORD

#include "daemon_state.h"
#include "config/config_types.h"
#include "process/process_manager.h"
#include "rpc/rpc_fwd.h"
#include <atomic>
#include <memory>

namespace supervisorcpp {

class Supervisord {
public:
    explicit Supervisord(const config::Configuration& config);
    ~Supervisord() = default;

    Supervisord(const Supervisord&) = delete;
    Supervisord& operator=(const Supervisord&) = delete;
    Supervisord(Supervisord&&) = delete;
    Supervisord& operator=(Supervisord&&) = delete;

    int run();

private:
    void register_rpc_handlers_();

    boost::asio::io_context   io_context_;
    config::Configuration     config_;
    std::atomic<DaemonState>  daemon_state_{DaemonState::RUNNING};
    process::ProcessManager   process_manager_;
    rpc::RpcServerPtr         rpc_server_ptr_;
};

} // namespace supervisorcpp

#endif // SUPERVISOR_APP__SUPERVISORD
