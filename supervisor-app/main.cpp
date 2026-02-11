/**
 * Unified entry point for supervisorcpp
 *
 * This binary can operate in two modes based on argv[0]:
 * - supervisord: Run as daemon (default)
 * - supervisorctl: Run as controller client
 *
 * Consistent with the busybox pattern
 */

#include <filesystem>
#include <string>
#include <cstring>

int supervisord_main(int argc, char* argv[]);
int supervisorctl_main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    // run as supervisord by default
    bool as_supervisorctl = false;
    int arg_ofs = 0;

    const auto program_name = std::filesystem::path{argv[0]}.filename().string();
    if (program_name == "supervisorctl") {
        as_supervisorctl = true; // invoked as supervisorctl
    } else if (argc > 1) {
        // Also support explicit mode selection via first argument
        if (std::strcmp(argv[1], "ctl") == 0 || std::strcmp(argv[1], "supervisorctl") == 0) {
            arg_ofs = 1; // shift arguments to remove mode selector
            as_supervisorctl = true;
        }
    }

    return (as_supervisorctl) 
        ? supervisorctl_main(argc - arg_ofs, argv + arg_ofs)
        : supervisord_main(argc, argv)
    ;
}
