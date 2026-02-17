// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__PROCESS_MANAGER
#define SUPERVISOR_LIB__PROCESS__PROCESS_MANAGER

#include "process.h"
#include "config/config_types.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <boost/asio.hpp>

namespace supervisorcpp::process {

/**
 * Manages all supervised processes
 */
class ProcessManager {
public:
    explicit ProcessManager(boost::asio::io_context& io_context,
                            std::chrono::milliseconds update_interval = std::chrono::seconds(1));
    ~ProcessManager();

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    ProcessManager(ProcessManager&&) = delete;
    ProcessManager& operator=(ProcessManager&&) = delete;

    /**
     * Add a process to be managed
     * @param config Process configuration
     */
    void add_process(const config::ProgramConfig& config);

    /**
     * Start all processes
     */
    void start_all();

    /**
     * Stop all processes
     */
    void stop_all();

    /**
     * Start a specific process by name
     * @return true if started successfully
     */
    bool start_process(const std::string& name);

    /**
     * Stop a specific process by name
     * @return true if stopped successfully
     */
    bool stop_process(const std::string& name);

    /**
     * Get process by name
     */
    const Process* get_process(const std::string& name) const;

    /**
     * Get all processes
     */
    const auto& get_processes() const noexcept {
        return processes_;
    }

    /**
     * Get all process info for status reporting
     */
    std::vector<ProcessInfo> get_all_process_info() const;

    /**
     * Check if any processes are running
     */
    bool has_running_processes() const;

private:
    /**
     * Begin SIGCHLD handler
     */
    void begin_signal_handler_();

    /**
     * Handle SIGCHLD signal
     */
    void handle_sigchld_();

    /**
     * Begin next periodic timer for process updates
     */
    void begin_update_timer_();

    /**
     * Periodic update callback
     */
    void on_timer_();

    boost::asio::io_context& io_context_;
    boost::asio::signal_set signals_;
    boost::asio::steady_timer timer_;

    std::vector<std::unique_ptr<Process>> processes_;
    std::map<std::string, Process*> process_map_;
    std::chrono::milliseconds update_interval_;
};

} // namespace supervisorcpp::process

#endif // SUPERVISOR_LIB__PROCESS__PROCESS_MANAGER
