#include "supervisorctl.h"
#include "args_parser.h"
#include "version.h"
#include "config/ini_reader.h"
#include <fstream>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

using SupervisorCtlClient = supervisorcpp::SupervisorCtlClient;

int supervisorctl_main(int argc, char* argv[]) {
    try {
        namespace po = boost::program_options;

        auto parsed_opt = supervisorcpp::parse_args(
            argc, argv,
            "supervisorctl - control supervisord processes",
            [](auto& parser, auto& desc) {
                (void)parser;
                desc.add_options()
                    ("serverurl,s", po::value<std::string>(), "Server URL (e.g. unix:///run/supervisord.sock)")
                ;
            },
            [] {
                SupervisorCtlClient{""}.cmdHelp(supervisorcpp::rpc::RpcParams{});
                std::cout << "\nUsage:\n";
                std::cout << "  supervisorctl status           # Show all process status\n";
                std::cout << "  supervisorctl start myapp      # Start a specific process\n";
                std::cout << "  supervisorctl                  # Interactive mode\n";
            }
        );
        if (!parsed_opt) return 0;
        auto& [ vm, args ] = *parsed_opt;

        // Resolve server URL: CLI arg > config file > default
        const auto serverurl = [&vm]() -> std::string {
            if (vm.count("serverurl")) return vm["serverurl"].as<std::string>();

            try {
                // Lightweight read of just [supervisorctl] from config
                std::ifstream file(vm["configuration"].as<std::string>());
                supervisorcpp::config::CommentStrippingBuf filtered(file);
                std::istream filtered_stream(&filtered);
                boost::property_tree::ptree tree;
                boost::property_tree::ini_parser::read_ini(filtered_stream, tree);
                return tree.get<std::string>("supervisorctl.serverurl", "unix:///run/supervisord.sock");
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not load config: " << e.what() << std::endl;
                return "unix:///run/supervisord.sock";
            }
        }();

        // Extract socket path from serverurl
        std::string socket_path;
        if (serverurl.starts_with("unix://")) {
            socket_path = serverurl.substr(7);
        } else if (serverurl.starts_with("/")) {
            socket_path = serverurl;
        } else {
            std::cerr << "Error: '" << serverurl << "' is not a valid server URL;"
                      << " use a unix:// URL or a socket path" << std::endl;
            return 1;
        }
        LOG_DEBUG << "Using socket: " << socket_path;

        // Create client
        SupervisorCtlClient client{socket_path};

        // Get remaining arguments as command
        if (args.empty()) {
            // No command specified, enter interactive mode
            client.interactive_mode();
        } else {
            // Execute single command and exit
            const auto method = args.front();
            args.erase(std::begin(args));
            return client.execute_command(method, args);
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
