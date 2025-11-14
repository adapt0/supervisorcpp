#include "config_parser.h"
#include "logger.h"
#include "process_manager.h"
#include "rpc_server.h"
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <csignal>

namespace po = boost::program_options;
using namespace supervisord;

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

        // Create IO context for async operations
        boost::asio::io_context io_context;

        // Create process manager
        process::ProcessManager process_manager(io_context);

        // Add all configured processes
        for (const auto& prog : config.programs) {
            process_manager.add_process(prog);
        }

        // Setup signal handlers for SIGTERM and SIGINT
        boost::asio::signal_set signals(io_context, SIGTERM, SIGINT);
        signals.async_wait([&](const boost::system::error_code& error, int signal_number) {
            if (!error) {
                LOG_INFO << "Received shutdown signal (" << signal_number << ")";
                process_manager.stop_all();
                io_context.stop();
            }
        });

        // Ignore SIGPIPE
        std::signal(SIGPIPE, SIG_IGN);

        // Create and start RPC server
        LOG_INFO << "Starting RPC server";
        rpc::RpcServer rpc_server(io_context, config.unix_http_server.socket_file.string(), process_manager);
        rpc_server.start();

        // Start all processes
        LOG_INFO << "Starting all configured processes";
        process_manager.start_all();

        if (nodaemon) {
            std::cout << "supervisord started successfully (press Ctrl+C to stop)" << std::endl;
        }

        // Run event loop
        LOG_INFO << "Main event loop starting";
        io_context.run();

        // Stop RPC server
        rpc_server.stop();

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
