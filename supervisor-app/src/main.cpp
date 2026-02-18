// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

/**
 * Unified entry point for supervisorcpp
 *
 * This binary can operate in two modes based on argv[0]:
 * - supervisord: Run as daemon
 * - supervisorctl: Run as controller client
 * - supervisor (or other): Defaults to ctl (tab-completion friendly)
 *
 * Consistent with the busybox pattern
 */

#include "logger/logger.h"
#include <algorithm>
#include <filesystem>
#include <string>

int supervisord_main(int argc, char* argv[]);
int supervisorctl_main(int argc, char* argv[], bool called_supervisorctl);

int main(int argc, char* argv[]) {
    supervisorcpp::logger::init_logging();

    // Default to ctl unless explicitly named supervisord or "d" arg is specified.
    // This avoids accidentally starting a second daemon when tab-completion
    // stops at "supervisor", and gives a useful ctl interface instead.
    const auto program_name = std::filesystem::path{argv[0]}.filename().string();
    if (program_name == "supervisorctl") {
        return supervisorctl_main(argc, argv, true);
    } else if (program_name == "supervisord") {
        return supervisord_main(argc, argv);
    } else if (argc > 1) {
        // mode selection, skipping over basic flags to allow for ./supervisor -vv -d
        for (int i = 1; i < argc; ++i) {
            if ('-' == argv[i][0]) {
                if (argv[i] != std::string_view{"-d"}
                    && argv[i] != std::string_view{"--daemon"}
                ) {
                    continue; // skip over options
                }
            } else if (argv[i] != std::string_view{"d"}) {
                break;
            }

            // remove daemon option by placing it at the end
            std::rotate(&argv[i], &argv[i + 1], &argv[argc]);
            --argc;
            return supervisord_main(argc, argv);
        }
    }

    return supervisorctl_main(argc, argv, false);
}
