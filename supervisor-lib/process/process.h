#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__PROCESS
#define SUPERVISOR_LIB__PROCESS__PROCESS

#include "../config/config_types.h"
#include <boost/asio.hpp>
#include <sys/types.h>
#include <chrono>
#include <memory>
#include <string>
#include <optional>
#include <array>

namespace supervisorcpp::logger {
    class LogWriter;
}

namespace supervisorcpp::process {

using TimePoint = std::chrono::steady_clock::time_point;

/**
 * Process state enumeration (supervisord compatible)
 */
enum class State {
    STOPPED = 0,
    STARTING = 10,
    RUNNING = 20,
    BACKOFF = 30,
    STOPPING = 40,
    EXITED = 100,
    FATAL = 200
};
std::ostream& operator<<(std::ostream& outs, State state);

/**
 * Information about a process for status reporting
 */
struct ProcessInfo {
    std::string name;
    State state;
    pid_t pid;
    int exitstatus;
    std::string stdout_logfile;
    std::string stderr_logfile;
    std::string spawnerr;  // Error message if spawn failed
    time_t now;
    time_t start;
    time_t stop;
    std::string description;
};

/**
 * Manages a single supervised process
 */
class Process {
public:
    explicit Process(boost::asio::io_context& io_context, const config::ProgramConfig& config);
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&) = delete;
    Process& operator=(Process&&) = delete;

    /**
     * Start the process
     * @return true if spawn succeeded, false otherwise
     */
    bool start();

    /**
     * Stop the process gracefully (SIGTERM, then SIGKILL after timeout)
     * @return true if stop initiated successfully
     */
    bool stop();

    /**
     * Kill the process immediately (SIGKILL)
     */
    void kill();

    /**
     * Handle process exit notification
     * @param exit_status Exit status from waitpid
     */
    void on_exit(int exit_status);

    /**
     * Update process state based on time elapsed
     * Called periodically by ProcessManager
     */
    void update();

    // Getters
    const std::string& name() const noexcept { return config_.name; }
    const config::ProgramConfig& config() const noexcept { return config_; }
    State state() const noexcept { return state_; }
    pid_t pid() const noexcept { return pid_; }
    bool is_running() const noexcept { return state_ == State::RUNNING; }
    bool should_autorestart() const noexcept { return config_.autorestart && state_ != State::FATAL; }
    int retry_count() const noexcept { return retry_count_; }

    /**
     * Get process info for status reporting
     */
    ProcessInfo get_info() const;

    /**
     * Get uptime in seconds (if running)
     */
    int get_uptime() const;

private:
    /**
     * Spawn the child process (fork + exec)
     * @return PID on success, -1 on failure
     */
    pid_t spawn();

    /**
     * Setup child process environment after fork
     * Called in child process before exec
     */
    void setup_child_process();

    /**
     * Switch to configured user (setuid/setgid)
     * Called in child process before exec
     */
    bool switch_user();

    /**
     * Setup working directory
     */
    bool setup_working_directory();

    /**
     * Setup environment variables
     */
    void setup_environment();

    /**
     * Setup stdout/stderr capture
     * @return true on success
     */
    bool setup_io_redirection();

    /**
     * Parse command line into argv array
     */
    std::vector<std::string> parse_command() const;

    /**
     * Transition to a new state
     */
    void set_state(State new_state);

    /**
     * Record spawn error
     */
    void set_spawn_error(const std::string& error);

    /**
     * Start async reading from stdout pipe
     */
    void start_stdout_read();

    /**
     * Handle stdout read completion
     */
    void handle_stdout_read_(const boost::system::error_code& error, size_t bytes_transferred);

    // Configuration
    config::ProgramConfig config_;

    // IO context reference
    boost::asio::io_context& io_context_;

    // State
    State state_;
    pid_t pid_;
    int exitstatus_;
    int retry_count_;
    std::string spawn_error_;

    // Timing
    TimePoint start_time_;
    TimePoint stop_time_;
    TimePoint state_change_time_;

    // Log writer
    std::unique_ptr<logger::LogWriter> log_writer_;

    // Async IO for stdout/stderr
    std::unique_ptr<boost::asio::posix::stream_descriptor> stdout_stream_;
    std::array<char, 4096> stdout_buffer_;
};

} // namespace supervisorcpp::process

#endif // SUPERVISOR_LIB__PROCESS__PROCESS
