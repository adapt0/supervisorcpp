#pragma once

#include "../util/errors.h"
#include <sys/resource.h>
#include <unistd.h>
#include <string>

namespace supervisord {
namespace process {

/**
 * Close all file descriptors except stdin/stdout/stderr
 * This prevents child processes from inheriting parent's file descriptors
 * (sockets, log files, etc.) which could cause:
 * - File descriptor leaks
 * - Child processes interfering with parent's files/sockets
 * - Security issues if child is compromised
 *
 * Note: This should be called AFTER stdout/stderr have been redirected
 * in the child process, but BEFORE exec.
 */
inline void close_inherited_fds() {
    // Get maximum number of file descriptors
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        // Close all fds from 3 to max (keeping 0=stdin, 1=stdout, 2=stderr)
        int max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : static_cast<int>(rl.rlim_max);
        for (int fd = 3; fd < max_fd; fd++) {
            close(fd);  // Ignore errors (fd may not be open)
        }
    }
}

/**
 * Set resource limits for child process
 */
inline void set_child_resource_limits() {
    struct rlimit rlim;

    // Limit number of processes (prevent fork bombs)
    rlim.rlim_cur = 100;  // Soft limit
    rlim.rlim_max = 200;  // Hard limit
    setrlimit(RLIMIT_NPROC, &rlim);

    // Limit address space to 4GB (prevent memory exhaustion)
    rlim.rlim_cur = 4UL * 1024 * 1024 * 1024;
    rlim.rlim_max = 4UL * 1024 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &rlim);

    // Limit core dump size to 0 (no core dumps)
    rlim.rlim_cur = 0;
    rlim.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &rlim);
}

/**
 * Verify privilege drop was successful
 * Checks that we're running as the target UID/GID
 */
inline void verify_privilege_drop(uid_t expected_uid, gid_t expected_gid) {
    uid_t real_uid = getuid();
    uid_t effective_uid = geteuid();
    gid_t real_gid = getgid();
    gid_t effective_gid = getegid();

    if (real_uid != expected_uid || effective_uid != expected_uid) {
        throw SecurityError("Privilege drop failed: UID mismatch (expected " +
                          std::to_string(expected_uid) +
                          ", got real=" + std::to_string(real_uid) +
                          ", effective=" + std::to_string(effective_uid) + ")");
    }

    if (real_gid != expected_gid || effective_gid != expected_gid) {
        throw SecurityError("Privilege drop failed: GID mismatch (expected " +
                          std::to_string(expected_gid) +
                          ", got real=" + std::to_string(real_gid) +
                          ", effective=" + std::to_string(effective_gid) + ")");
    }
}

} // namespace process
} // namespace supervisord
