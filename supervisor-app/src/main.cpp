// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

/**
 * Unified entry point for supervisorcpp
 *
 * This binary can operate in two modes based on argv[0]:
 * - supervisord: Run as daemon (default)
 * - supervisorctl: Run as controller client
 *
 * Consistent with the busybox pattern
 */

#include "logger/logger.h"
#include <algorithm>
#include <filesystem>
#include <string>

int supervisord_main(int argc, char* argv[]);
int supervisorctl_main(int argc, char* argv[]);

int main(int argc, char* argv[]) {
    supervisorcpp::logger::init_logging();

    // default to supervisord, unless we are named supervisorctl, or a ctl arg is specified
    const auto program_name = std::filesystem::path{argv[0]}.filename().string();
    if (program_name == "supervisord") {
        // always supervisord
    } else if (program_name == "supervisorctl") {
        return supervisorctl_main(argc, argv); // invoked as supervisorctl
    } else if (argc > 1) {
        // mode selection, skipping over basic flags to allow for ./supervisor -vv ctl status
        for (int i = 1; i < argc; ++i) {
            if ('-' == argv[i][0]) continue; // skip over options
            if (argv[i] != std::string_view{"ctl"}) break;

            // remove "ctl" by placing it at the end
            std::rotate(&argv[i], &argv[i + 1], &argv[argc]);
            --argc;
            return supervisorctl_main(argc, argv); // invoked as supervisorctl
        }
    }

    return supervisord_main(argc, argv);
}
