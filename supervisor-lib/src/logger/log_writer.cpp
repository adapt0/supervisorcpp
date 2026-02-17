// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "log_writer.h"
#include "logger.h"
#include <algorithm>
#include <sstream>
#include <sys/stat.h>

namespace supervisorcpp::logger {

LogWriter::LogWriter(const std::filesystem::path& logfile,
                     size_t max_bytes,
                     int backups)
: logfile_(logfile)
, max_bytes_(max_bytes)
, backups_(backups)
, current_size_(0)
{
    ensure_directory_();
    openNL_();
}

LogWriter::~LogWriter() {
    closeNL_();
}

bool LogWriter::openNL_() {
    // Close existing file if open
    if (file_.is_open()) {
        file_.close();
    }

    // Open file in append mode
    file_.open(logfile_, std::ios::app | std::ios::binary);
    if (!file_.is_open()) {
        LOG_ERROR << "Failed to open log file: " << logfile_;
        return false;
    }

    // Get current file size
    try {
        if (std::filesystem::exists(logfile_)) {
            current_size_ = std::filesystem::file_size(logfile_);
        } else {
            current_size_ = 0;
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to get file size for " << logfile_ << ": " << e.what();
        current_size_ = 0;
    }

    LOG_DEBUG << "Opened log file: " << logfile_ << " (current size: " << current_size_ << ")";
    return true;
}

ssize_t LogWriter::write(const std::string& data) {
    if (data.empty()) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!file_.is_open()) {
        if (!openNL_()) return -1;
    }

    // Add to pending buffer
    pending_buffer_ += data;

    // Find last newline
    size_t last_newline = pending_buffer_.rfind('\n');

    if (last_newline != std::string::npos) {
        // We have complete lines to write
        std::string to_write = pending_buffer_.substr(0, last_newline + 1);
        pending_buffer_ = pending_buffer_.substr(last_newline + 1);

        // Check if we need to rotate before writing
        if (max_bytes_ > 0 && current_size_ + to_write.size() > max_bytes_) {
            rotateNL_();
        }

        // Write the complete lines
        file_.write(to_write.c_str(), to_write.size());
        if (!file_) {
            LOG_ERROR << "Failed to write to log file: " << logfile_;
            return -1;
        }

        current_size_ += to_write.size();
        file_.flush();  // Flush after each line boundary

        return to_write.size();
    }

    // No complete lines yet, just buffered
    return 0;
}

ssize_t LogWriter::write_line(const std::string& line) {
    std::string data = line;
    if (data.empty() || data.back() != '\n') {
        data += '\n';
    }
    return write(data);
}

void LogWriter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Write any pending buffer (even if incomplete line)
    if (!pending_buffer_.empty() && file_.is_open()) {
        file_.write(pending_buffer_.c_str(), pending_buffer_.size());
        current_size_ += pending_buffer_.size();
        pending_buffer_.clear();
    }

    if (file_.is_open()) {
        file_.flush();
    }
}

void LogWriter::closeNL_() {
    if (!file_.is_open()) return;

    // Write any pending buffer
    if (!pending_buffer_.empty()) {
        file_.write(pending_buffer_.c_str(), pending_buffer_.size());
        current_size_ += pending_buffer_.size();
        pending_buffer_.clear();
    }

    file_.close();
}

void LogWriter::rotateNL_() {
    LOG_INFO << "Rotating log file: " << logfile_;

    // Close current file
    if (file_.is_open()) file_.close();

    // Rotate existing backups
    // logfile.9 -> delete
    // logfile.8 -> logfile.9
    // ...
    // logfile.1 -> logfile.2
    // logfile -> logfile.1

    try {
        // Delete the oldest backup if it exists
        if (backups_ > 0) {
            std::filesystem::path oldest = logfile_.string() + "." + std::to_string(backups_);
            if (std::filesystem::exists(oldest)) {
                std::filesystem::remove(oldest);
            }
        }

        // Rotate backups
        for (int i = backups_ - 1; i >= 1; --i) {
            std::filesystem::path from = logfile_.string() + "." + std::to_string(i);
            std::filesystem::path to = logfile_.string() + "." + std::to_string(i + 1);

            if (std::filesystem::exists(from)) {
                std::filesystem::rename(from, to);
            }
        }

        // Rotate current log file
        if (std::filesystem::exists(logfile_)) {
            std::filesystem::path backup = logfile_.string() + ".1";
            std::filesystem::rename(logfile_, backup);
        }

    } catch (const std::exception& e) {
        LOG_ERROR << "Error during log rotation: " << e.what();
    }

    // Open new log file
    current_size_ = 0;
    openNL_();
}

bool LogWriter::ensure_directory_() {
    try {
        std::filesystem::path dir = logfile_.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            LOG_INFO << "Creating log directory: " << dir;
            std::filesystem::create_directories(dir);
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to create log directory: " << e.what();
        return false;
    }
}

} // namespace supervisorcpp::process
