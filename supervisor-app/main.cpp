/**
 * Unified entry point for supervisorcpp
 *
 * This binary can operate in two modes based on argv[0]:
 * - supervisord: Run as daemon (default)
 * - supervisorctl: Run as controller client
 *
 * This follows the busybox pattern used in Alpine Linux.
 */

#include <filesystem>
#include <string>
#include <cstring>

// Forward declarations for mode entry points
int supervisord_main(int argc, char* argv[]);
int supervisorctl_main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    // Get the basename of argv[0] to determine mode
    std::filesystem::path program_path(argv[0]);
    std::string program_name = program_path.filename().string();

    // Check if invoked as supervisorctl
    if (program_name == "supervisorctl" || program_name == "supervisorctl.cpp") {
        return supervisorctl_main(argc, argv);
    }

    // Also support explicit mode selection via first argument
    if (argc > 1) {
        if (std::strcmp(argv[1], "ctl") == 0 || std::strcmp(argv[1], "supervisorctl") == 0) {
            // Shift arguments to remove mode selector
            return supervisorctl_main(argc - 1, argv + 1);
        }
    }

    // Default: run as supervisord
    return supervisord_main(argc, argv);
}
