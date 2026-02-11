#include "process.h"
#include "../logger/log_writer.h"
#include "../logger/logger.h"
#include "../util/platform.h"
#include "../util/secure.h"
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


Process::Process(boost::asio::io_context& io_context, const config::ProgramConfig& config)
: config_(config)
, io_context_(io_context)
, state_(State::STOPPED)
, pid_(-1)
, exitstatus_(0)
, retry_count_(0)
, state_change_time_(std::chrono::steady_clock::now())
{
    // Create log writer if logging is configured
    if (config_.stdout_logfile.has_value()) {
        log_writer_ = std::make_unique<logger::LogWriter>(
            *config_.stdout_logfile,
            config_.stdout_logfile_maxbytes,
            config_.stdout_logfile_backups
        );
    }

    LOG_INFO << "Process '" << config_.name << "' created";
}

Process::~Process() {
    if (pid_ > 0) {
        LOG_WARN << "Process '" << config_.name << "' destroyed while running (pid " << pid_ << "), killing";
        kill();
    }

    // Close stream descriptors
    if (stdout_stream_ && stdout_stream_->is_open()) {
        boost::system::error_code ec;
        stdout_stream_->close(ec);
    }

    // Close log writer
    if (log_writer_) log_writer_.reset();
}

bool Process::start() {
    if (state_ == State::RUNNING || state_ == State::STARTING) {
        LOG_WARN << "Process '" << config_.name << "' already running or starting";
        return false;
    }

    LOG_INFO << "Starting process '" << config_.name << "'";

    pid_t child_pid = spawn();
    if (child_pid < 0) {
        LOG_ERROR << "Failed to spawn process '" << config_.name << "': " << spawn_error_;

        retry_count_++;
        if (retry_count_ >= config_.startretries) {
            LOG_ERROR << "Process '" << config_.name << "' failed to start after "
                     << retry_count_ << " retries, entering FATAL state";
            set_state(State::FATAL);
        } else {
            LOG_INFO << "Process '" << config_.name << "' will retry starting (attempt "
                    << retry_count_ + 1 << " of " << config_.startretries << ")";
            set_state(State::BACKOFF);
        }
        return false;
    }

    pid_ = child_pid;
    start_time_ = std::chrono::steady_clock::now();
    set_state(State::STARTING);

    LOG_INFO << "Process '" << config_.name << "' started with pid " << pid_;
    return true;
}

bool Process::stop() {
    if (pid_ <= 0 || state_ == State::STOPPED) {
        LOG_WARN << "Process '" << config_.name << "' not running";
        return false;
    }

    LOG_INFO << "Stopping process '" << config_.name << "' (pid " << pid_ << ")";
    set_state(State::STOPPING);
    stop_time_ = std::chrono::steady_clock::now();

    // Send stop signal (default SIGTERM)
    int sig = SIGTERM;
    if (config_.stopsignal == "INT") sig = SIGINT;
    else if (config_.stopsignal == "QUIT") sig = SIGQUIT;
    else if (config_.stopsignal == "KILL") sig = SIGKILL;
    else if (config_.stopsignal == "HUP") sig = SIGHUP;

    if (::kill(pid_, sig) == 0) {
        LOG_DEBUG << "Sent signal " << sig << " to process " << pid_;
        return true;
    } else {
        LOG_ERROR << "Failed to send signal to process " << pid_ << ": " << strerror(errno);
        return false;
    }
}

void Process::kill() {
    if (pid_ <= 0) {
        return;
    }

    LOG_INFO << "Killing process '" << config_.name << "' (pid " << pid_ << ")";
    ::kill(pid_, SIGKILL);

    // Wait for process to exit
    int status;
    waitpid(pid_, &status, 0);

    pid_ = -1;
    set_state(State::STOPPED);
}

void Process::on_exit(int exit_status) {
    exitstatus_ = exit_status;

    if (WIFEXITED(exit_status)) {
        int code = WEXITSTATUS(exit_status);
        LOG_INFO << "Process '" << config_.name << "' (pid " << pid_
                << ") exited with code " << code;
        exitstatus_ = code;
    } else if (WIFSIGNALED(exit_status)) {
        int sig = WTERMSIG(exit_status);
        LOG_INFO << "Process '" << config_.name << "' (pid " << pid_
                << ") killed by signal " << sig;
        exitstatus_ = -sig;
    }

    pid_ = -1;

    // Handle state transitions
    if (state_ == State::STOPPING) {
        // Normal stop
        set_state(State::STOPPED);
        retry_count_ = 0;  // Reset retry count on clean stop
    } else if (should_autorestart()) {
        // Unexpected exit with autorestart
        LOG_INFO << "Process '" << config_.name << "' will be restarted";
        retry_count_++;

        if (retry_count_ >= config_.startretries) {
            LOG_ERROR << "Process '" << config_.name << "' failed after "
                     << retry_count_ << " retries, entering FATAL state";
            set_state(State::FATAL);
        } else {
            set_state(State::BACKOFF);
        }
    } else {
        // Exit without autorestart
        set_state(State::EXITED);
    }
}

void Process::update() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state_change_time_).count();

    switch (state_) {
        case State::STARTING:
            // Check if process has been running long enough to be considered "started"
            if (elapsed >= config_.startsecs) {
                LOG_INFO << "Process '" << config_.name << "' successfully started";
                set_state(State::RUNNING);
                retry_count_ = 0;  // Reset retry count on successful start
            }
            break;

        case State::STOPPING:
            // Check if we need to SIGKILL after timeout
            if (elapsed >= config_.stopwaitsecs) {
                LOG_WARN << "Process '" << config_.name << "' did not stop gracefully, sending SIGKILL";
                ::kill(pid_, SIGKILL);
            }
            break;

        case State::BACKOFF:
            // Wait a bit before retrying (exponential backoff could be added here)
            if (elapsed >= 1) {  // Wait 1 second before retry
                LOG_INFO << "Retrying start of process '" << config_.name << "'";
                start();
            }
            break;

        default:
            break;
    }
}

ProcessInfo Process::get_info() const {
    ProcessInfo info{
        .name = config_.name,
        .state = state_,
        .pid = pid_,
        .exitstatus = exitstatus_,
        .stdout_logfile = config_.stdout_logfile.has_value() ? config_.stdout_logfile->string() : "",
        .stderr_logfile = "",  // We redirect stderr to stdout
        .spawnerr = spawn_error_
    };

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

    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
}

pid_t Process::spawn() {
    spawn_error_.clear();

    // Create pipes for stdout/stderr if logging is configured
    // Use pipe2() with O_CLOEXEC for better security/robustness:
    // - Parent's read end gets CLOEXEC set automatically
    // - If parent ever exec()s (shouldn't happen), won't leak FDs
    // - Child's write end will have CLOEXEC cleared by dup2()
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (config_.stdout_logfile.has_value()) {
        if (pipe2(stdout_pipe, O_CLOEXEC) < 0) {
            set_spawn_error("Failed to create stdout pipe: " + std::string(strerror(errno)));
            return -1;
        }
    }

    if (!config_.redirect_stderr && config_.stdout_logfile.has_value()) {
        if (pipe2(stderr_pipe, O_CLOEXEC) < 0) {
            set_spawn_error("Failed to create stderr pipe: " + std::string(strerror(errno)));
            if (stdout_pipe[0] >= 0) {
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
            }
            return -1;
        }
    }

    pid_t child_pid = fork();

    if (child_pid < 0) {
        // Fork failed
        set_spawn_error("Fork failed: " + std::string(strerror(errno)));

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
        setup_child_process();

        // If we get here, exec failed
        _exit(127);
    }

    // Parent process

    // Close write ends of pipes
    if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
    if (stderr_pipe[1] >= 0) close(stderr_pipe[1]);

    // Setup async reading from stdout pipe
    if (stdout_pipe[0] >= 0 && log_writer_) {
        try {
            // Set non-blocking (preserve existing flags)
            int flags = fcntl(stdout_pipe[0], F_GETFL, 0);
            fcntl(stdout_pipe[0], F_SETFL, flags | O_NONBLOCK);

            // Create stream descriptor for async reading
            stdout_stream_ = std::make_unique<boost::asio::posix::stream_descriptor>(
                io_context_, stdout_pipe[0]
            );

            // Start async reading
            start_stdout_read();

        } catch (const std::exception& e) {
            LOG_ERROR << "Failed to setup async reading for stdout: " << e.what();
            close(stdout_pipe[0]);
        }
    } else if (stdout_pipe[0] >= 0) {
        // No log writer configured, just close the pipe
        close(stdout_pipe[0]);
    }

    // For now, we don't handle stderr separately (redirect_stderr handles it)
    if (stderr_pipe[0] >= 0) {
        close(stderr_pipe[0]);
    }

    return child_pid;
}

void Process::setup_child_process() {
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
    if (!setup_working_directory()) {
        LOG_ERROR << "Failed to setup working directory";
        _exit(126);
    }

    // Setup environment
    setup_environment();

    // Switch user (must be last before exec)
    if (!switch_user()) {
        LOG_ERROR << "Failed to switch user";
        _exit(125);
    }

    // Parse command
    auto args = parse_command();

    // Build argv array
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    // Execute
    execvp(argv[0], argv.data());

    // If we get here, exec failed
    LOG_ERROR << "exec failed for '" << config_.name << "': " << strerror(errno);
}

bool Process::switch_user() {
    if (config_.user.empty() || config_.user == "root") {
        // No user switching needed (or we're already root)
        return true;
    }

    // Only root can switch users
    if (getuid() != 0) {
        LOG_WARN << "Not running as root, cannot switch to user '" << config_.user << "'";
        return true;  // Continue anyway
    }

    // Look up user
    struct passwd* pwd = getpwnam(config_.user.c_str());
    if (!pwd) {
        LOG_ERROR << "User '" << config_.user << "' not found";
        return false;
    }

    // Initialize supplementary groups
    if (initgroups(config_.user.c_str(), pwd->pw_gid) < 0) {
        LOG_ERROR << "initgroups failed: " << strerror(errno);
        return false;
    }

    // Set group ID
    if (setgid(pwd->pw_gid) < 0) {
        LOG_ERROR << "setgid failed: " << strerror(errno);
        return false;
    }

    // Set user ID
    if (setuid(pwd->pw_uid) < 0) {
        LOG_ERROR << "setuid failed: " << strerror(errno);
        return false;
    }

    // SECURITY: Verify privilege drop was successful
    try {
        util::verify_privilege_drop(pwd->pw_uid, pwd->pw_gid);
    } catch (const util::SecurityError& e) {
        LOG_ERROR << "Privilege drop verification failed: " << e.what();
        return false;
    }

    LOG_DEBUG << "Switched to user '" << config_.user << "' (uid=" << pwd->pw_uid
              << ", gid=" << pwd->pw_gid << ")";

    return true;
}

bool Process::setup_working_directory() {
    if (!config_.directory.has_value()) {
        return true;  // No directory change needed
    }

    if (chdir(config_.directory->string().c_str()) < 0) {
        LOG_ERROR << "chdir to '" << config_.directory->string() << "' failed: " << strerror(errno);
        return false;
    }

    LOG_DEBUG << "Changed to directory '" << config_.directory->string() << "'";
    return true;
}

void Process::setup_environment() {
    // Add configured environment variables
    for (const auto& [key, value] : config_.environment) {
        setenv(key.c_str(), value.c_str(), 1);  // 1 = overwrite
        LOG_DEBUG << "Set environment: " << key << "=" << value;
    }
}

std::vector<std::string> Process::parse_command() const {
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

void Process::set_state(State new_state) {
    if (new_state != state_) {
        LOG_DEBUG << "Process '" << config_.name << "' " << state_ << " -> " << new_state;
        state_ = new_state;
        state_change_time_ = std::chrono::steady_clock::now();
    }
}

void Process::set_spawn_error(const std::string& error) {
    spawn_error_ = error;
    LOG_ERROR << "Spawn error for '" << config_.name << "': " << error;
}

void Process::start_stdout_read() {
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
            LOG_DEBUG << "Process '" << config_.name << "' closed stdout";
        } else if (error != boost::asio::error::operation_aborted) {
            LOG_ERROR << "Error reading from process '" << config_.name << "' stdout: " << error.message();
        }

        // Close the stream
        if (stdout_stream_ && stdout_stream_->is_open()) {
            boost::system::error_code close_error;
            stdout_stream_->close(close_error);
        }

        // Flush any remaining data in log writer
        if (log_writer_) {
            log_writer_->flush();
        }

        return;
    }

    if (bytes_transferred > 0 && log_writer_) {
        // Write to log file
        std::string data(stdout_buffer_.data(), bytes_transferred);
        log_writer_->write(data);
    }

    // Continue reading
    start_stdout_read();
}

} // namespace supervisorcpp::process
