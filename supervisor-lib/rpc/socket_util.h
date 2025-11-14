#pragma once

#include "../util/errors.h"
#include <filesystem>
#include <sys/stat.h>

namespace supervisord {
namespace rpc {

/**
 * Set secure permissions on Unix socket
 * chmod 0600 (owner read/write only)
 */
inline void set_socket_permissions(const std::filesystem::path& socket_path) {
    if (chmod(socket_path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        throw SecurityError("Failed to set socket permissions: " + socket_path.string());
    }
}

} // namespace rpc
} // namespace supervisord
