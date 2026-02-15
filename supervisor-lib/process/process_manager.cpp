#include "process_manager.h"
#include "../logger/logger.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <algorithm>
#include <thread>

namespace supervisorcpp::process {

ProcessManager::ProcessManager(boost::asio::io_context& io_context,
                               std::chrono::milliseconds update_interval)
: io_context_(io_context)
, signals_(io_context, SIGCHLD)
, timer_(io_context)
, update_interval_(update_interval)
{
    LOG_INFO << "ProcessManager initialized";
    begin_signal_handler_();
    begin_update_timer_();
}

ProcessManager::~ProcessManager() {
    LOG_INFO << "ProcessManager shutting down";
    stop_all();
}

void ProcessManager::add_process(const config::ProgramConfig& config) {
    auto process_uptr = std::make_unique<Process>(io_context_, config);
    Process* proc_raw = process_uptr.get();

    processes_.push_back(std::move(process_uptr));
    process_map_[config.name] = proc_raw;

    LOG_INFO << "Added process '" << config.name << "' to manager";
}

void ProcessManager::start_all() {
    LOG_INFO << "Starting all processes (" << processes_.size() << " total)";

    for (auto& process : processes_) {
        process->start();
    }
}

void ProcessManager::stop_all() {
    LOG_INFO << "Stopping all processes";

    // Send stop signal to all processes with active PIDs (RUNNING, STARTING, etc.)
    bool any_signaled = false;
    for (auto& process : processes_) {
        if (process->pid() > 0) {
            process->stop();
            any_signaled = true;
        }
    }

    if (!any_signaled) return;

    // Actively reap children instead of blocking sleep
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (has_running_processes() && std::chrono::steady_clock::now() < deadline) {
        int status;
        const pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            LOG_DEBUG << "Reaped child process " << pid << ", status=" << status;
            for (auto& process : processes_) {
                if (process->pid() == pid) {
                    process->on_exit(status);
                    break;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Force kill any remaining processes
    for (auto& process : processes_) {
        if (process->pid() > 0) {
            LOG_WARN << "Force killing process '" << process->name() << "'";
            process->kill();
        }
    }
}

bool ProcessManager::start_process(const std::string& name) {
    const auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    return it->second->start();
}

bool ProcessManager::stop_process(const std::string& name) {
    const auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    return it->second->stop();
}

bool ProcessManager::restart_process(const std::string& name) {
    const auto it = process_map_.find(name);
    if (std::end(process_map_) == it) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }
    auto* proc = it->second;

    if (proc->pid() > 0) {
        if (!proc->stop()) {
            return false;
        }

        // Actively reap until stopped or timeout
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (proc->pid() > 0 && std::chrono::steady_clock::now() < deadline) {
            int status;
            pid_t pid = waitpid(proc->pid(), &status, WNOHANG);
            if (pid > 0) {
                proc->on_exit(status);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    return proc->start();
}

const Process* ProcessManager::get_process(const std::string& name) const {
    const auto it = process_map_.find(name);
    return (std::end(process_map_) != it) ? it->second : nullptr;
}

std::vector<ProcessInfo> ProcessManager::get_all_process_info() const {
    std::vector<ProcessInfo> info_list;
    info_list.reserve(processes_.size());

    for (const auto& process : processes_) {
        info_list.push_back(process->get_info());
    }

    return info_list;
}

bool ProcessManager::has_running_processes() const {
    return std::any_of(
        std::begin(processes_), std::end(processes_),
        [](const auto& p) { return p->pid() > 0; }
    );
}

void ProcessManager::begin_signal_handler_() {
    signals_.async_wait([this](const boost::system::error_code& error, int /*signal_number*/) {
        if (error) return;

        handle_sigchld_();
        begin_signal_handler_();
    });
}

void ProcessManager::handle_sigchld_() {
    // Reap all exited children
    while (true) {
        int status;
        const pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            break;  // No more children to reap
        }

        LOG_DEBUG << "Reaped child process " << pid << ", status=" << status;

        // Find the process and notify it
        for (auto& process : processes_) {
            if (process->pid() == pid) {
                process->on_exit(status);
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
    for (auto& process : processes_) {
        process->update();
    }

    // Schedule next update
    begin_update_timer_();
}

} // namespace supervisorcpp::process
