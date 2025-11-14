#include "process_manager.h"
#include "logger.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <algorithm>

namespace supervisord {
namespace process {

ProcessManager::ProcessManager(boost::asio::io_context& io_context)
    : io_context_(io_context)
    , signals_(io_context, SIGCHLD)
    , timer_(io_context)
{
    LOG_INFO << "ProcessManager initialized";
    setup_signal_handler();
    setup_update_timer();
}

ProcessManager::~ProcessManager() {
    LOG_INFO << "ProcessManager shutting down";
    stop_all();
}

void ProcessManager::add_process(const config::ProgramConfig& config) {
    auto process = std::make_unique<Process>(config);
    Process* proc_ptr = process.get();

    processes_.push_back(std::move(process));
    process_map_[config.name] = proc_ptr;

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

    for (auto& process : processes_) {
        if (process->is_running()) {
            process->stop();
        }
    }

    // Wait a bit for graceful shutdown
    // In a real implementation, we'd wait for processes to actually exit
    // For now, we'll just give them a moment
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Kill any remaining processes
    for (auto& process : processes_) {
        if (process->pid() > 0) {
            LOG_WARN << "Force killing process '" << process->name() << "'";
            process->kill();
        }
    }
}

bool ProcessManager::start_process(const std::string& name) {
    auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    return it->second->start();
}

bool ProcessManager::stop_process(const std::string& name) {
    auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    return it->second->stop();
}

bool ProcessManager::restart_process(const std::string& name) {
    auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        LOG_ERROR << "Process '" << name << "' not found";
        return false;
    }

    Process* proc = it->second;

    if (proc->is_running()) {
        if (!proc->stop()) {
            return false;
        }

        // Wait for process to actually stop
        // In production, this should be handled asynchronously
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return proc->start();
}

Process* ProcessManager::get_process(const std::string& name) {
    auto it = process_map_.find(name);
    if (it == process_map_.end()) {
        return nullptr;
    }
    return it->second;
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
    return std::any_of(processes_.begin(), processes_.end(),
                      [](const auto& p) { return p->pid() > 0; });
}

void ProcessManager::setup_signal_handler() {
    signals_.async_wait([this](const boost::system::error_code& error, int /*signal_number*/) {
        if (!error) {
            handle_sigchld();
            // Re-register handler
            setup_signal_handler();
        }
    });
}

void ProcessManager::handle_sigchld() {
    // Reap all exited children
    while (true) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

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

void ProcessManager::setup_update_timer() {
    timer_.expires_after(std::chrono::seconds(1));
    timer_.async_wait([this](const boost::system::error_code& error) {
        if (!error) {
            on_timer();
        }
    });
}

void ProcessManager::on_timer() {
    // Update all processes
    for (auto& process : processes_) {
        process->update();
    }

    // Schedule next update
    setup_update_timer();
}

} // namespace process
} // namespace supervisord
