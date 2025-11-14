#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <mutex>

namespace supervisord {
namespace process {

/**
 * Handles writing to log files with size-based rotation
 */
class LogWriter {
public:
    /**
     * Create a log writer
     * @param logfile Path to log file
     * @param max_bytes Maximum file size before rotation (0 = no rotation)
     * @param backups Number of backup files to keep
     */
    LogWriter(const std::filesystem::path& logfile,
              size_t max_bytes = 50 * 1024 * 1024,
              int backups = 10);

    ~LogWriter();

    // Disable copy, allow move
    LogWriter(const LogWriter&) = delete;
    LogWriter& operator=(const LogWriter&) = delete;
    LogWriter(LogWriter&&) = default;
    LogWriter& operator=(LogWriter&&) = default;

    /**
     * Write data to log file
     * Rotation happens on line boundaries (after \n)
     * @param data Data to write
     * @return Number of bytes written, or -1 on error
     */
    ssize_t write(const std::string& data);

    /**
     * Write a line to log file (appends \n if not present)
     * @param line Line to write
     * @return Number of bytes written, or -1 on error
     */
    ssize_t write_line(const std::string& line);

    /**
     * Flush pending output
     */
    void flush();

    /**
     * Close the log file
     */
    void close();

    /**
     * Get current file size
     */
    size_t current_size() const { return current_size_; }

    /**
     * Get log file path
     */
    const std::filesystem::path& path() const { return logfile_; }

private:
    /**
     * Open or reopen the log file
     */
    bool open();

    /**
     * Rotate log files
     * Renames logfile -> logfile.1, logfile.1 -> logfile.2, etc.
     */
    void rotate();

    /**
     * Check if rotation is needed and rotate if necessary
     * Only rotates on line boundaries (after \n in buffer)
     */
    void check_rotation();

    /**
     * Ensure log directory exists
     */
    bool ensure_directory();

    // Configuration
    std::filesystem::path logfile_;
    size_t max_bytes_;
    int backups_;

    // State
    std::ofstream file_;
    size_t current_size_;
    std::string pending_buffer_;  // Buffer for partial lines
    std::mutex mutex_;  // Thread safety
};

} // namespace process
} // namespace supervisord
