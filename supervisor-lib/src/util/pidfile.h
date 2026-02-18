// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__UTIL__PIDFILE
#define SUPERVISOR_LIB__UTIL__PIDFILE

#include "secure.h"
#include "logger/logger.h"
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <sys/file.h>

namespace supervisorcpp::util {

/**
 * RAII guard for pidfile management
 *
 * Creates pidfile on construction with advisory flock, removes it on destruction.
 * The lock prevents multiple daemon instances from using the same pidfile.
 * Move-only to prevent accidental copies.
 */
class PidFileGuard {
public:
    /**
     * Create, lock, and write pidfile
     * @param path Path to pidfile
     * @throws std::runtime_error if pidfile cannot be created, locked, or written
     */
    explicit PidFileGuard(std::filesystem::path path)
    : path_{validate_pidfile_path(std::move(path))}
    {
        fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open pidfile: " + path_.string()
                + " - " + std::strerror(errno));
        }

        if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("Another instance is already running (pidfile locked): "
                + path_.string());
        }

        const auto pid_str = std::to_string(::getpid()) + '\n';
        if (::write(fd_, pid_str.data(), pid_str.size()) < 0) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error("Failed to write pidfile: " + path_.string()
                + " - " + std::strerror(errno));
        }

        LOG_DEBUG << "Wrote pidfile: " << path_.string();
    }

    ~PidFileGuard() {
        if (fd_ < 0) return;  // moved-from

        ::close(fd_);  // releases flock

        std::error_code ec;
        std::filesystem::remove(path_, ec);
        if (!ec) {
            LOG_DEBUG << "Removed pidfile: " << path_;
        } else {
            LOG_WARN << "Failed to remove pidfile: " << path_ << " - " << ec.message();
        }
    }

    PidFileGuard(PidFileGuard&& other) noexcept
    : path_{std::move(other.path_)}, fd_{other.fd_}
    {
        other.fd_ = -1;
    }

    PidFileGuard& operator=(PidFileGuard&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            path_ = std::move(other.path_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    PidFileGuard(const PidFileGuard&) = delete;
    PidFileGuard& operator=(const PidFileGuard&) = delete;

private:
    std::filesystem::path   path_;
    int                     fd_{-1};
};

} // namespace supervisorcpp::util

#endif // SUPERVISOR_LIB__UTIL__PIDFILE
