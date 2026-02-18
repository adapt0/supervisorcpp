// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "process.h"
#include "logger/log_writer.h"
#include "logger/logger.h"
#include "util/platform.h"
#include "util/secure.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sstream>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace supervisorcpp::process {

std::ostream& operator<<(std::ostream& outs, State state) {
    switch (state) {
    case State::STOPPED: return outs << "STOPPED";
    case State::STARTING: return outs << "STARTING";
    case State::RUNNING: return outs << "RUNNING";
    case State::BACKOFF: return outs << "BACKOFF";
    case State::STOPPING: return outs << "STOPPING";
    case State::EXITED: return outs << "EXITED";
    case State::FATAL: return outs << "FATAL";
    }
    return outs << "Unknown (" << static_cast<int>(state) << ')';
}

std::ostream& operator<<(std::ostream& outs, const Process& process) {
    return outs << "'" << process.name() << "' ";
}

Process::Process(boost::asio::io_context& io_context, const config::ProgramConfig& config)
: config_(config)
, io_context_(io_context)
, state_(State::STOPPED)
, pid_(-1)
, exitstatus_(0)
, retry_count_(0)
, state_change_time_(std::chrono::steady_clock::now())
{
    // Create log writers if logging is configured
    const auto create_writer = [](const config::ProgramConfig::Log& log) {
        if (log.file.has_value()) {
            return std::make_unique<logger::LogWriter>(
                *log.file,
                log.file_maxbytes,
                log.file_backups
            );
        } else {
            return std::unique_ptr<logger::LogWriter>();
        }
    };
    stdout_log_writer_ = create_writer(config_.stdout_log);
    stderr_log_writer_ = create_writer(config_.stderr_log);

    LOG_TRACE << *this << "Created";
}

Process::~Process() {
    if (pid_ > 0) {
        LOG_WARN << *this << "Destroyed while running (pid " << pid_ << "), killing";
        kill();
    }

    // Close stream descriptors
    if (stdout_stream_ && stdout_stream_->is_open()) {
        boost::system::error_code ec;
        stdout_stream_->close(ec);
    }
    if (stderr_stream_ && stderr_stream_->is_open()) {
        boost::system::error_code ec;
        stderr_stream_->close(ec);
    }
}

bool Process::start() {
    if (state_ == State::RUNNING || state_ == State::STARTING) {
        LOG_DEBUG << *this << "Already running or starting";
        return true;
    }

    const pid_t child_pid = spawn_();
    if (child_pid < 0) {
        LOG_ERROR << *this << "Failed to spawn process - " << spawn_error_;

        retry_count_++;
        if (retry_count_ >= config_.startretries) {
            LOG_ERROR << *this << "failed to start after " << retry_count_ << " retries, entering FATAL state";
            set_state_(State::FATAL);
        } else {
            LOG_INFO << *this << "Will retry starting (attempt " << retry_count_ + 1 << " of " << config_.startretries << ")";
            set_state_(State::BACKOFF);
        }
        return false;
    }

    pid_ = child_pid;
    start_time_ = std::chrono::steady_clock::now();
    set_state_(State::STARTING);

    return true;
}

bool Process::stop() {
    if (pid_ <= 0 || state_ == State::STOPPED) {
        LOG_WARN << *this << "Not running";
        return false;
    }

    LOG_DEBUG << *this << "Stopping (pid " << pid_ << ")";
    set_state_(State::STOPPING);
    stop_time_ = std::chrono::steady_clock::now();

    // Send stop signal (default SIGTERM)
    const auto sig = [this] {
        if (config_.stopsignal == "INT") return SIGINT;
        if (config_.stopsignal == "QUIT") return SIGQUIT;
        if (config_.stopsignal == "KILL") return SIGKILL;
        if (config_.stopsignal == "HUP") return SIGHUP;
        return SIGTERM;
    }();

    if (::kill(pid_, sig) == 0) {
        LOG_DEBUG << *this << "Sent signal " << sig;
        return true;
    } else {
        LOG_ERROR << *this << "Failed to send signal to process - " << strerror(errno);
        return false;
    }
}

void Process::kill() {
    if (pid_ <= 0) return;

    LOG_INFO << *this << "Killing process (pid " << pid_ << ")";
    ::kill(pid_, SIGKILL);

    // Wait for process to exit
    int status;
    waitpid(pid_, &status, 0);

    pid_ = -1;
    set_state_(State::STOPPED);
}

void Process::on_exit(int exit_status) {
    exitstatus_ = exit_status;

    if (WIFEXITED(exit_status)) {
        const int code = WEXITSTATUS(exit_status);
        LOG_INFO << *this << "(pid " << pid_ << ") exited with code " << code;
        exitstatus_ = code;
    } else if (WIFSIGNALED(exit_status)) {
        const int sig = WTERMSIG(exit_status);
        LOG_INFO << *this << "(pid " << pid_ << ") killed by signal " << sig;
        exitstatus_ = -sig;
    }

    pid_ = -1;

    // Handle state transitions
    if (state_ == State::STOPPING) {
        // Normal stop
        set_state_(State::STOPPED);
        retry_count_ = 0;  // Reset retry count on clean stop
    } else if (should_autorestart()) {
        // Unexpected exit with autorestart
        LOG_DEBUG << *this << "Will be restarted";
        retry_count_++;

        if (retry_count_ >= config_.startretries) {
            LOG_ERROR << *this << "Failed after " << retry_count_ << " retries, entering FATAL state";
            set_state_(State::FATAL);
        } else {
            set_state_(State::BACKOFF);
        }
    } else {
        // Exit without autorestart
        set_state_(State::EXITED);
    }
}

void Process::update() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state_change_time_).count();

    switch (state_) {
    case State::STARTING:
        // Check if process has been running long enough to be considered "started"
        if (elapsed >= config_.startsecs) {
            set_state_(State::RUNNING);
            retry_count_ = 0;  // Reset retry count on successful start
        }
        break;

    case State::STOPPING:
        // Check if we need to SIGKILL after timeout
        if (elapsed >= config_.stopwaitsecs) {
            LOG_WARN << *this << "Did not stop gracefully, sending SIGKILL";
            ::kill(pid_, SIGKILL);
        }
        break;

    case State::BACKOFF: {
        // Exponential backoff: 1, 2, 4, 8, ... seconds, capped at 60
        const auto delay = std::min(1 << retry_count_, 60);
        if (elapsed >= delay) {
            LOG_INFO << *this << "Retrying start (attempt " << (retry_count_ + 1)
                     << "/" << config_.startretries << ", waited " << delay << "s)";
            start();
        }
        break;
    }

    default:
        break;
    }
}

ProcessInfo Process::get_info() const {
    ProcessInfo info;
    info.name = config_.name;
    info.state = state_;
    info.pid = pid_;
    info.exitstatus = exitstatus_;
    info.stdout_logfile = config_.stdout_log.file.has_value() ? config_.stdout_log.file->string() : "";
    info.stderr_logfile = config_.stderr_log.file.has_value() ? config_.stderr_log.file->string() : "";
    info.spawnerr = spawn_error_;

    const auto now_time = std::chrono::system_clock::now();
    info.now = std::chrono::system_clock::to_time_t(now_time);

    if (state_ == State::RUNNING || state_ == State::STARTING) {
        const auto start_sys = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() -
            (std::chrono::steady_clock::now() - start_time_)
        );
        info.start = std::chrono::system_clock::to_time_t(start_sys);
        info.stop = 0;

        const int uptime = get_uptime();
        std::ostringstream desc;
        desc << "pid " << pid_ << ", uptime " << (uptime / 3600) << ":"
             << std::setfill('0') << std::setw(2) << ((uptime % 3600) / 60) << ":"
             << std::setfill('0') << std::setw(2) << (uptime % 60);
        info.description = desc.str();
    } else {
        info.start = 0;
        info.stop = 0;
        info.description = "";
    }

    return info;
}

int Process::get_uptime() const {
    if (state_ != State::RUNNING && state_ != State::STARTING) {
        return 0;
    }

    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
}

pid_t Process::spawn_() {
    spawn_error_.clear();

    LOG_DEBUG << *this << "spawn: \"" << config_.command << '"';

    // Create pipes for stdout/stderr if logging is configured
    // Use pipe2() with O_CLOEXEC for better security/robustness:
    // - Parent's read end gets CLOEXEC set automatically
    // - If parent ever exec()s (shouldn't happen), won't leak FDs
    // - Child's write end will have CLOEXEC cleared by dup2()
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (config_.stdout_log.file.has_value()) {
        if (pipe2(stdout_pipe, O_CLOEXEC) < 0) {
            set_spawn_error_("Failed to create stdout pipe: " + std::string(strerror(errno)));
            return -1;
        }
    }

    if (!config_.redirect_stderr && config_.stderr_log.file.has_value()) {
        if (pipe2(stderr_pipe, O_CLOEXEC) < 0) {
            set_spawn_error_("Failed to create stderr pipe: " + std::string(strerror(errno)));
            if (stdout_pipe[0] >= 0) {
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
            }
            return -1;
        }
    }

    const pid_t child_pid = fork();
    if (child_pid < 0) {
        // Fork failed
        set_spawn_error_("Fork failed: " + std::string(strerror(errno)));

        if (stdout_pipe[0] >= 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        if (stderr_pipe[0] >= 0) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }

        return -1;
    }

    if (child_pid == 0) {
        // Child process

        // Close read ends of pipes
        if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
        if (stderr_pipe[0] >= 0) close(stderr_pipe[0]);

        // Redirect stdout
        if (stdout_pipe[1] >= 0) {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdout_pipe[1]);
        }

        // Redirect stderr
        if (config_.redirect_stderr && stdout_pipe[1] >= 0) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
        } else if (stderr_pipe[1] >= 0) {
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[1]);
        }

        // Setup child process environment
        setup_child_process_();

        // If we get here, exec failed
        _exit(127);
    }

    // Parent process

    // Close write ends of pipes
    if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
    if (stderr_pipe[1] >= 0) close(stderr_pipe[1]);

    // Setup async reading from stdout pipe
    if (stdout_pipe[0] >= 0 && stdout_log_writer_) {
        try {
            // Set non-blocking (preserve existing flags)
            const int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
            fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

            // Create stream descriptor for async reading
            stdout_stream_ = std::make_unique<boost::asio::posix::stream_descriptor>(
                io_context_, stdout_pipe[0]
            );

            // Start async reading
            start_stdout_read_();

        } catch (const std::exception& e) {
            LOG_ERROR << *this << "Failed to setup async reading for stdout - " << e.what();
            close(stdout_pipe[0]);
        }
    } else if (stdout_pipe[0] >= 0) {
        // No log writer configured, just close the pipe
        close(stdout_pipe[0]);
    }

    // Setup async reading from stderr pipe
    if (stderr_pipe[0] >= 0 && stderr_log_writer_) {
        try {
            const int flags = fcntl(stderr_pipe[0], F_GETFL, 0);
            fcntl(stderr_pipe[0], F_SETFL, flags | O_NONBLOCK);

            stderr_stream_ = std::make_unique<boost::asio::posix::stream_descriptor>(
                io_context_, stderr_pipe[0]
            );

            start_stderr_read_();

        } catch (const std::exception& e) {
            LOG_ERROR << *this << "Failed to setup async reading for stderr - " << e.what();
            close(stderr_pipe[0]);
        }
    } else if (stderr_pipe[0] >= 0) {
        close(stderr_pipe[0]);
    }

    return child_pid;
}

void Process::setup_child_process_() {
    // Reset signal handlers
    signal(SIGPIPE, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);

    // SECURITY: Close all inherited file descriptors (except stdin/stdout/stderr)
    // This prevents child from accessing parent's sockets, log files, etc.
    // Must be called AFTER stdio redirection in spawn()
    util::close_inherited_fds();

    // Setup working directory
    if (!setup_working_directory_()) {
        LOG_ERROR << *this << "Failed to setup working directory";
        _exit(126);
    }

    // Setup environment
    setup_environment_();

    // Apply per-process umask if configured
    if (config_.umask.has_value()) {
        ::umask(*config_.umask);
    }

    // Switch user (must be last before exec)
    if (!switch_user_()) {
        LOG_ERROR << *this << "Failed to switch user";
        _exit(125);
    }

    // Parse command
    const auto args = parse_command_();

    // Build argv array
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    // Execute
    execvp(argv[0], argv.data());

    // If we get here, exec failed
    LOG_ERROR << *this << "exec failed - " << strerror(errno);
}

bool Process::switch_user_() {
    if (config_.user.empty() || config_.user == "root") {
        // No user switching needed (or we're already root)
        return true;
    }

    // Only root can switch users
    if (getuid() != 0) {
        LOG_WARN << *this << "Not running as root, cannot switch to user '" << config_.user << "'";
        return true;  // Continue anyway
    }

    // Look up user
    const auto* pwd = getpwnam(config_.user.c_str());
    if (!pwd) {
        LOG_ERROR << *this << "User '" << config_.user << "' not found";
        return false;
    }

    // Initialize supplementary groups
    if (initgroups(config_.user.c_str(), pwd->pw_gid) < 0) {
        LOG_ERROR << *this << "initgroups failed: " << strerror(errno);
        return false;
    }

    // Set group ID
    if (setgid(pwd->pw_gid) < 0) {
        LOG_ERROR << *this << "setgid failed: " << strerror(errno);
        return false;
    }

    // Set user ID
    if (setuid(pwd->pw_uid) < 0) {
        LOG_ERROR << *this << "setuid failed: " << strerror(errno);
        return false;
    }

    // SECURITY: Verify privilege drop was successful
    try {
        util::verify_privilege_drop(pwd->pw_uid, pwd->pw_gid);
    } catch (const util::SecurityError& e) {
        LOG_ERROR << *this << "Privilege drop verification failed - " << e.what();
        return false;
    }

    LOG_DEBUG << *this << "Switched to user '" << config_.user << "' (uid=" << pwd->pw_uid << ", gid=" << pwd->pw_gid << ")";

    return true;
}

bool Process::setup_working_directory_() {
    if (!config_.directory.has_value()) {
        return true;  // No directory change needed
    }

    if (chdir(config_.directory->string().c_str()) < 0) {
        LOG_ERROR << *this << "chdir to '" << config_.directory->string() << "' failed: " << strerror(errno);
        return false;
    }

    LOG_DEBUG << *this << "Changed to directory '" << config_.directory->string() << "'";
    return true;
}

void Process::setup_environment_() {
    // Set supervisor-specific environment variables
    // These come before user env vars so they can be overridden if needed
    setenv("SUPERVISOR_ENABLED", "1", 1);
    setenv("SUPERVISOR_PROCESS_NAME", config_.name.c_str(), 1);
    setenv("SUPERVISOR_GROUP_NAME",   config_.name.c_str(), 1);

    // Add configured environment variables
    for (const auto& [key, value] : config_.environment) {
        setenv(key.c_str(), value.c_str(), 1);  // 1 = overwrite
        LOG_DEBUG << *this << "Set environment: " << key << "=" << value;
    }
}

std::vector<std::string> Process::parse_command_() const {
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    bool escape_next = false;

    for (char c : config_.command) {
        if (escape_next) {
            current += c;
            escape_next = false;
        } else if (c == '\\') {
            escape_next = true;
        } else if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        args.push_back(current);
    }

    return args;
}

void Process::set_state_(State new_state) {
    if (new_state != state_) {
        if (pid_ > 0) {
            LOG_INFO << *this << "(pid: " << pid_ << ") " << state_ << " -> " << new_state;
        } else {
            LOG_INFO << *this << state_ << " -> " << new_state;
        }
        state_ = new_state;
        state_change_time_ = std::chrono::steady_clock::now();
    }
}

void Process::set_spawn_error_(const std::string& error) {
    spawn_error_ = error;
    LOG_ERROR << *this << "Spawn error - " << error;
}

void Process::start_stdout_read_() {
    if (!stdout_stream_ || !stdout_stream_->is_open()) {
        return;
    }

    stdout_stream_->async_read_some(
        boost::asio::buffer(stdout_buffer_),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            handle_stdout_read_(error, bytes_transferred);
        }
    );
}

void Process::handle_stdout_read_(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
        if (error == boost::asio::error::eof) {
            // End of stream - process closed stdout
            LOG_DEBUG << *this << "Closed stdout";
        } else if (error != boost::asio::error::operation_aborted) {
            LOG_ERROR << *this << "Error reading from process stdout - " << error.message();
        }

        // Close the stream
        if (stdout_stream_ && stdout_stream_->is_open()) {
            boost::system::error_code close_error;
            stdout_stream_->close(close_error);
        }

        // Flush any remaining data in log writer
        if (stdout_log_writer_) {
            stdout_log_writer_->flush();
        }

        return;
    }

    if (bytes_transferred > 0 && stdout_log_writer_) {
        // Write to log file
        std::string data(stdout_buffer_.data(), bytes_transferred);
        stdout_log_writer_->write(data);
    }

    // Continue reading
    start_stdout_read_();
}

void Process::start_stderr_read_() {
    if (!stderr_stream_ || !stderr_stream_->is_open()) {
        return;
    }

    stderr_stream_->async_read_some(
        boost::asio::buffer(stderr_buffer_),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            handle_stderr_read_(error, bytes_transferred);
        }
    );
}

void Process::handle_stderr_read_(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
        if (error == boost::asio::error::eof) {
            LOG_DEBUG << *this << "Closed stderr";
        } else if (error != boost::asio::error::operation_aborted) {
            LOG_ERROR << *this << "Error reading from process stderr - " << error.message();
        }

        if (stderr_stream_ && stderr_stream_->is_open()) {
            boost::system::error_code close_error;
            stderr_stream_->close(close_error);
        }

        if (stderr_log_writer_) {
            stderr_log_writer_->flush();
        }

        return;
    }

    if (bytes_transferred > 0 && stderr_log_writer_) {
        std::string data(stderr_buffer_.data(), bytes_transferred);
        stderr_log_writer_->write(data);
    }

    start_stderr_read_();
}

} // namespace supervisorcpp::process
