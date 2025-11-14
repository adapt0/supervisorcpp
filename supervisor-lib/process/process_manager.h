#pragma once

#include "process.h"
#include "../config/config_types.h"
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace supervisord {
namespace process {

/**
 * Manages all supervised processes
 */
class ProcessManager {
public:
    explicit ProcessManager(boost::asio::io_context& io_context);
    ~ProcessManager();

    // Disable copy and move
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
     * Restart a specific process by name
     * @return true if restarted successfully
     */
    bool restart_process(const std::string& name);

    /**
     * Get process by name
     */
    Process* get_process(const std::string& name);

    /**
     * Get all processes
     */
    const std::vector<std::unique_ptr<Process>>& get_processes() const {
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
     * Setup SIGCHLD handler
     */
    void setup_signal_handler();

    /**
     * Handle SIGCHLD signal
     */
    void handle_sigchld();

    /**
     * Setup periodic timer for process updates
     */
    void setup_update_timer();

    /**
     * Periodic update callback
     */
    void on_timer();

    // IO context for async operations
    boost::asio::io_context& io_context_;

    // Signal set for SIGCHLD
    boost::asio::signal_set signals_;

    // Timer for periodic updates
    boost::asio::steady_timer timer_;

    // All managed processes
    std::vector<std::unique_ptr<Process>> processes_;

    // Map for quick lookup by name
    std::map<std::string, Process*> process_map_;
};

} // namespace process
} // namespace supervisord
