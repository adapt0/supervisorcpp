#pragma once
#ifndef SUPERVISOR_LIB__UTIL__PIDFILE
#define SUPERVISOR_LIB__UTIL__PIDFILE

#include "secure.h"
#include "logger/logger.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace supervisorcpp::util {

/**
 * RAII guard for pidfile management
 *
 * Creates pidfile on construction, removes it on destruction.
 * Move-only to prevent accidental copies.
 */
class PidFileGuard {
public:
    /**
     * Create and write pidfile
     * @param path Path to pidfile
     * @throws std::runtime_error if pidfile cannot be written
     */
    explicit PidFileGuard(std::filesystem::path path)
    : path_{validate_pidfile_path(std::move(path))}
    {
        {
            std::ofstream pidfile{path_};
            if (!pidfile) throw std::runtime_error("Failed to write pidfile: " + path_.string());
            pidfile << getpid() << '\n';
        }

        LOG_DEBUG << "Wrote pidfile: " << path_.string();
    }

    ~PidFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        if (!ec) {
            LOG_DEBUG << "Removed pidfile: " << path_;
        } else {
            LOG_WARN << "Failed to remove pidfile: " << path_ << " - " << ec.message();
        }
    }

    // Move-only
    PidFileGuard(PidFileGuard&& other) noexcept = default;
    PidFileGuard& operator=(PidFileGuard&& other) noexcept = default;

    PidFileGuard(const PidFileGuard&) = delete;
    PidFileGuard& operator=(const PidFileGuard&) = delete;

private:
    std::filesystem::path   path_;
};

} // namespace supervisorcpp::util

#endif // SUPERVISOR_LIB__UTIL__PIDFILE
