#pragma once

#include "errors.h"
#include <filesystem>
#include <string>

namespace supervisord {
namespace util {

/**
 * Canonicalize path and validate it doesn't escape allowed directory
 * Returns absolute path with symlinks resolved
 */
inline std::filesystem::path canonicalize_path(const std::filesystem::path& path,
                                                const std::filesystem::path& allowed_prefix) {
    namespace fs = std::filesystem;

    // Resolve to canonical path (resolves symlinks, . and ..)
    fs::path canonical;
    try {
        canonical = fs::canonical(path);
    } catch (const fs::filesystem_error& e) {
        // Path doesn't exist - try to canonicalize parent and append filename
        fs::path parent = path.parent_path();
        fs::path filename = path.filename();

        if (parent.empty()) {
            parent = fs::current_path();
        }

        try {
            canonical = fs::canonical(parent) / filename;
        } catch (const fs::filesystem_error&) {
            throw SecurityError("Cannot resolve path: " + path.string());
        }
    }

    // Check if canonical path starts with allowed prefix
    std::string canonical_str = canonical.string();
    std::string prefix_str = allowed_prefix.string();

    if (canonical_str.find(prefix_str) != 0) {
        throw SecurityError("Path escapes allowed directory: " + path.string() +
                          " (resolved to: " + canonical_str +
                          ", allowed prefix: " + prefix_str + ")");
    }

    return canonical;
}

} // namespace util
} // namespace supervisord
