// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "process_manager.h"
#include "process.h"
#include "logger/logger.h"
#include <algorithm>
#include <set>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>

namespace supervisorcpp::process {

ProcessManager::ProcessManager(boost::asio::io_context& io_context,
                               std::chrono::milliseconds update_interval)
: io_context_(io_context)
, signals_(io_context, SIGCHLD)
, timer_(io_context)
, update_interval_(update_interval)
{
    begin_signal_handler_();
    begin_update_timer_();
    LOG_TRACE << "ProcessManager initialized";
}

ProcessManager::~ProcessManager() {
    LOG_TRACE << "ProcessManager shutting down";
    stop_all();
    LOG_TRACE << "ProcessManager shut down";
}

void ProcessManager::add_process(const config::ProgramConfig& config) {
    process_map_[config.name] = Process::create(io_context_, config);
    LOG_DEBUG << "Added process '" << config.name << "' to manager";
}

void ProcessManager::start_all() {
    if (process_map_.empty()) return;
    LOG_INFO << "Starting all processes (" << process_map_.size() << " total)";

    for (const auto& [_, process_ptr] : process_map_) {
        process_ptr->start();
    }
}

void ProcessManager::stop_all() {
    if (process_map_.empty()) return;

    // Send stop signal to all processes with active PIDs (RUNNING, STARTING, etc.)
    int signaled = 0;
    for (const auto& [_, process_ptr] : process_map_) {
        if (process_ptr->pid() > 0) {
            process_ptr->stop();
            signaled++;
        }
    }

    if (0 == signaled) return;
    LOG_INFO << "Stopping " << signaled << " processes";

    // Wait long enough for per-process SIGKILL timers to fire (process.cpp checks stopwaitsecs)
    int max_wait = 0;
    for (const auto& [_, process_ptr] : process_map_) {
        if (process_ptr->pid() > 0) {
            max_wait = std::max(max_wait, process_ptr->config().stopwaitsecs);
        }
    }
    reap_until_(
        std::chrono::seconds(max_wait + 1),
        [this] { return has_running_processes(); }
    );

    // Force kill any remaining processes
    for (const auto& [_, process_ptr] : process_map_) {
        if (process_ptr->pid() > 0) {
            LOG_WARN << "Force killing process '" << process_ptr->name() << "'";
            process_ptr->kill();
        }
    }
}

bool ProcessManager::start_process(const std::string& name) {
    const auto it = process_map_.find(name);
    if (std::end(process_map_) == it) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    const auto process_ptr = it->second;
    return (process_ptr) ? process_ptr->start() : false;
}

bool ProcessManager::stop_process(const std::string& name) {
    const auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    const auto process_ptr = it->second;
    if (!process_ptr || !process_ptr->stop()) return false;

    // Wait for process to actually exit (reap it synchronously)
    reap_until_(
        std::chrono::seconds(process_ptr->config().stopwaitsecs + 1),
        [&process_ptr] { return process_ptr->pid() > 0; }
    );
    return true;
}

bool ProcessManager::remove_process(const std::string& name) {
    const auto it = process_map_.find(name);
    if (it == process_map_.end()) return false;

    // Stop the process first if running
    if (it->second->pid() > 0) {
        stop_process(name);
    }

    process_map_.erase(it);

    LOG_INFO << "Removed process '" << name << "'";
    return true;
}

void ProcessManager::sync_processes(
    const std::vector<config::ProgramConfig>& new_programs)
{
    size_t changed = 0;
    {
        // Build set of new program names
        std::set<std::string> new_names;
        for (const auto& prog : new_programs) {
            new_names.insert(prog.name);
        }

        // removed programs (in current but not in new)
        for (auto it = std::begin(process_map_); it != std::end(process_map_); ) {
            const auto& name = it->first;
            ++it;

            if (!new_names.contains(name)) {
                ++changed;
                remove_process(name);
            }
        }
    }

    // Find added and changed programs
    for (const auto& prog : new_programs) {
        const auto it = process_map_.find(prog.name);
        if (it == std::end(process_map_)) {
            // New program
            add_process(prog);
            start_process(prog.name);
            ++changed;
        } else if (!(it->second->config() == prog)) {
            // Config changed — restart with new config
            LOG_INFO << "Config changed for process '" << prog.name << "', restarting";
            remove_process(prog.name);
            add_process(prog);
            start_process(prog.name);
            ++changed;
        }
        // Unchanged — leave running
    }

    if (0 == changed) {
        LOG_INFO << "Process configuration unchanged";
    }
}

ProcessConstPtr ProcessManager::get_process(const std::string& name) const {
    const auto it = process_map_.find(name);
    return (std::end(process_map_) != it) ? it->second : ProcessConstPtr{};
}

std::vector<ProcessInfo> ProcessManager::get_all_process_info() const {
    std::vector<ProcessInfo> info_list;
    info_list.reserve(process_map_.size());

    for (const auto& [_, process_ptr] : process_map_) {
        info_list.push_back(process_ptr->get_info());
    }

    return info_list;
}

bool ProcessManager::has_running_processes() const {
    for (const auto& [_, process_ptr] : process_map_) {
        if (process_ptr->pid() > 0) return true;
    }
    return false;
}

void ProcessManager::reap_until_(std::chrono::milliseconds duration_ms,
                                  std::function<bool()> keep_waiting) {
    using steady_clock = std::chrono::steady_clock;

    for (const auto deadline = steady_clock::now() + duration_ms;
        keep_waiting() && steady_clock::now() < deadline;
    ) {
        if (handle_sigchld_()) continue;
        io_context_.run_for(std::chrono::milliseconds(10));
    }
}

void ProcessManager::begin_signal_handler_() {
    signals_.async_wait([this](const boost::system::error_code& error, int /*signal_number*/) {
        if (error) return;

        handle_sigchld_();
        begin_signal_handler_();
    });
}

bool ProcessManager::handle_sigchld_() {
    // Reap all exited children
    bool reaped = false;
    while (true) {
        int status;
        const pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            return reaped;  // No more children to reap
        }

        LOG_DEBUG << "Reaped child process (PID: " << pid << ", status=" << status << ')';
        reaped = true;

        // Find the process and notify it
        for (const auto& [_, process_ptr] : process_map_) {
            if (process_ptr->pid() == pid) {
                process_ptr->on_exit(status);
                break;
            }
        }
    }
}

void ProcessManager::begin_update_timer_() {
    timer_.expires_after(update_interval_);
    timer_.async_wait([this](const boost::system::error_code& error) {
        if (!error) {
            on_timer_();
        }
    });
}

void ProcessManager::on_timer_() {
    // Update all processes
    for (const auto& [_, process_ptr] : process_map_) {
        process_ptr->update();
    }

    // Schedule next update
    begin_update_timer_();
}

} // namespace supervisorcpp::process
