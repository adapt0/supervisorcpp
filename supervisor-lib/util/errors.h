#pragma once

#include <stdexcept>
#include <string>

namespace supervisord {

/**
 * Security validation exception
 * Thrown when security checks fail (ownership, permissions, paths, etc.)
 */
class SecurityError : public std::runtime_error {
public:
    explicit SecurityError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace supervisord
