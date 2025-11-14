#include "config_parser.h"
#include "logger.h"
#include <boost/program_options.hpp>
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

namespace po = boost::program_options;
using namespace supervisord;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGTERM || signal == SIGINT) {
        LOG_INFO << "Received shutdown signal (" << signal << ")";
        g_running = false;
    }
}

void setup_signal_handlers() {
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGPIPE, SIG_IGN); // Ignore broken pipe
}

int main(int argc, char* argv[]) {
    try {
        // Parse command line options
        po::options_description desc("supervisord - process control system");
        desc.add_options()
            ("help,h", "Show this help message")
            ("version,v", "Show version information")
            ("config,c", po::value<std::string>()->default_value("/etc/supervisord.conf"),
             "Configuration file path")
            ("nodaemon,n", "Run in foreground (don't daemonize)")
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return 0;
        }

        if (vm.count("version")) {
            std::cout << "supervisord 0.1.0 (C++ minimal replacement)" << std::endl;
            return 0;
        }

        std::string config_file = vm["config"].as<std::string>();
        bool nodaemon = vm.count("nodaemon") > 0;

        // Load configuration
        std::cout << "Loading configuration from: " << config_file << std::endl;
        config::Configuration config = config::ConfigParser::parse_file(config_file);

        // Initialize logging
        util::init_logging(config.supervisord.logfile, config.supervisord.loglevel);

        LOG_INFO << "supervisord starting (C++ version 0.1.0)";
        LOG_INFO << "Configuration file: " << config_file;
        LOG_INFO << "Socket file: " << config.unix_http_server.socket_file;
        LOG_INFO << "Found " << config.programs.size() << " program(s) to manage";

        // Log program information
        for (const auto& prog : config.programs) {
            LOG_INFO << "  - " << prog.name << ": " << prog.command;
        }

        // Setup signal handlers
        setup_signal_handlers();

        // Main loop (placeholder for Phase 2)
        LOG_INFO << "Main event loop starting (placeholder - Phase 1 complete)";

        if (nodaemon) {
            std::cout << "supervisord started successfully (press Ctrl+C to stop)" << std::endl;
        }

        // Simple loop for now
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        LOG_INFO << "supervisord shutting down";
        util::shutdown_logging();

        return 0;

    } catch (const config::ConfigParseError& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
