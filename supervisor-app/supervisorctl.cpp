#include "config/config_parser.h"
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>

namespace po = boost::program_options;
namespace pt = boost::property_tree;

/**
 * XML-RPC client for supervisorctl
 */
class SupervisorctlClient {
public:
    SupervisorctlClient(const std::string& socket_path)
        : socket_path_(socket_path)
        , io_context_()
        , socket_(io_context_)
    {
    }

    /**
     * Call RPC method
     */
    std::string call_method(const std::string& method_name,
                           const std::vector<std::string>& params = {}) {
        try {
            // Connect to Unix socket
            boost::asio::local::stream_protocol::endpoint endpoint(socket_path_);
            socket_.connect(endpoint);

            // Build XML-RPC request
            std::string request = build_xmlrpc_request(method_name, params);
            std::string http_request = build_http_request(request);

            // Send request
            boost::asio::write(socket_, boost::asio::buffer(http_request));

            // Read response
            boost::asio::streambuf response;
            boost::asio::read_until(socket_, response, "\r\n\r\n");

            // Read remaining body
            std::string response_str;
            {
                std::istream response_stream(&response);
                std::string line;
                size_t content_length = 0;

                // Parse headers
                while (std::getline(response_stream, line) && line != "\r") {
                    if (line.find("Content-Length:") == 0) {
                        content_length = std::stoull(line.substr(15));
                    }
                }

                // Read body
                if (content_length > 0) {
                    response_str.resize(content_length);
                    size_t bytes_read = response_stream.read(&response_str[0], content_length).gcount();

                    if (bytes_read < content_length) {
                        // Need to read more from socket
                        size_t remaining = content_length - bytes_read;
                        boost::asio::read(socket_, boost::asio::buffer(&response_str[bytes_read], remaining));
                    }
                }
            }

            socket_.close();

            return response_str;

        } catch (const std::exception& e) {
            throw std::runtime_error("RPC error: " + std::string(e.what()));
        }
    }

private:
    std::string build_xmlrpc_request(const std::string& method_name,
                                     const std::vector<std::string>& params) {
        std::ostringstream oss;
        oss << "<?xml version=\"1.0\"?>\n";
        oss << "<methodCall>\n";
        oss << "  <methodName>" << method_name << "</methodName>\n";
        oss << "  <params>\n";

        for (const auto& param : params) {
            oss << "    <param>\n";
            oss << "      <value><string>" << param << "</string></value>\n";
            oss << "    </param>\n";
        }

        oss << "  </params>\n";
        oss << "</methodCall>\n";
        return oss.str();
    }

    std::string build_http_request(const std::string& body) {
        std::ostringstream oss;
        oss << "POST /RPC2 HTTP/1.1\r\n";
        oss << "Content-Type: text/xml\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << body;
        return oss.str();
    }

    std::string socket_path_;
    boost::asio::io_context io_context_;
    boost::asio::local::stream_protocol::socket socket_;
};

/**
 * Parse process info from XML-RPC response
 */
struct ProcessInfo {
    std::string name;
    std::string statename;
    int pid;
    std::string description;
};

std::vector<ProcessInfo> parse_process_info_array(const std::string& xml) {
    std::vector<ProcessInfo> result;

    try {
        std::istringstream iss(xml);
        pt::ptree tree;
        pt::read_xml(iss, tree);

        // Navigate to the array
        auto value_node = tree.get_child("methodResponse.params.param.value");

        // Check if it's an array
        if (auto array_node = value_node.get_child_optional("array.data")) {
            for (const auto& item : *array_node) {
                if (item.first == "value") {
                    ProcessInfo info;

                    // Parse struct members
                    if (auto struct_node = item.second.get_child_optional("struct")) {
                        for (const auto& member : *struct_node) {
                            if (member.first == "member") {
                                std::string name = member.second.get<std::string>("name");

                                if (name == "name") {
                                    info.name = member.second.get<std::string>("value.string", "");
                                } else if (name == "statename") {
                                    info.statename = member.second.get<std::string>("value.string", "UNKNOWN");
                                } else if (name == "pid") {
                                    info.pid = member.second.get<int>("value.int", -1);
                                } else if (name == "description") {
                                    info.description = member.second.get<std::string>("value.string", "");
                                }
                            }
                        }
                    }

                    result.push_back(info);
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse response: " + std::string(e.what()));
    }

    return result;
}

/**
 * Format process info for display (supervisord-compatible format)
 */
void print_process_info(const std::vector<ProcessInfo>& processes) {
    for (const auto& proc : processes) {
        std::cout << std::left << std::setw(20) << proc.name
                  << std::setw(15) << proc.statename
                  << proc.description << std::endl;
    }
}

/**
 * Execute a supervisorctl command
 */
int execute_command(SupervisorctlClient& client, const std::vector<std::string>& args) {
    if (args.empty()) {
        return 0;
    }

    std::string cmd = args[0];

    try {
        if (cmd == "status") {
            // Get all process info
            std::string response = client.call_method("supervisor.getAllProcessInfo");
            auto processes = parse_process_info_array(response);
            print_process_info(processes);

        } else if (cmd == "start") {
            if (args.size() < 2) {
                std::cerr << "Error: process name required" << std::endl;
                return 1;
            }

            if (args[1] == "all") {
                client.call_method("supervisor.startAllProcesses");
                std::cout << "All processes started" << std::endl;
            } else {
                client.call_method("supervisor.startProcess", {args[1]});
                std::cout << args[1] << ": started" << std::endl;
            }

        } else if (cmd == "stop") {
            if (args.size() < 2) {
                std::cerr << "Error: process name required" << std::endl;
                return 1;
            }

            if (args[1] == "all") {
                client.call_method("supervisor.stopAllProcesses");
                std::cout << "All processes stopped" << std::endl;
            } else {
                client.call_method("supervisor.stopProcess", {args[1]});
                std::cout << args[1] << ": stopped" << std::endl;
            }

        } else if (cmd == "restart") {
            if (args.size() < 2) {
                std::cerr << "Error: process name required" << std::endl;
                return 1;
            }

            if (args[1] == "all") {
                client.call_method("supervisor.stopAllProcesses");
                client.call_method("supervisor.startAllProcesses");
                std::cout << "All processes restarted" << std::endl;
            } else {
                client.call_method("supervisor.restart", {args[1]});
                std::cout << args[1] << ": restarted" << std::endl;
            }

        } else if (cmd == "shutdown") {
            client.call_method("supervisor.shutdown");
            std::cout << "Shutting down supervisord" << std::endl;

        } else if (cmd == "reload") {
            std::cout << "Restarting supervisord..." << std::endl;
            client.call_method("supervisor.shutdown");

        } else if (cmd == "help") {
            std::cout << "Available commands:\n";
            std::cout << "  status [name]         Show process status\n";
            std::cout << "  start <name|all>      Start process(es)\n";
            std::cout << "  stop <name|all>       Stop process(es)\n";
            std::cout << "  restart <name|all>    Restart process(es)\n";
            std::cout << "  shutdown              Shutdown supervisord\n";
            std::cout << "  reload                Reload configuration\n";
            std::cout << "  help                  Show this help\n";
            std::cout << "  exit/quit             Exit supervisorctl\n";

        } else if (cmd == "exit" || cmd == "quit") {
            return -1;  // Signal to exit

        } else {
            std::cerr << "*** Unknown command: " << cmd << std::endl;
            std::cerr << "Use 'help' for available commands" << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

/**
 * Interactive mode
 */
void interactive_mode(SupervisorctlClient& client) {
    std::cout << "supervisord> ";
    std::string line;

    while (std::getline(std::cin, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if (line.empty()) {
            std::cout << "supervisord> ";
            continue;
        }

        // Split into words
        std::vector<std::string> args;
        std::istringstream iss(line);
        std::string word;
        while (iss >> word) {
            args.push_back(word);
        }

        int result = execute_command(client, args);
        if (result == -1) {
            break;  // Exit requested
        }

        std::cout << "supervisord> ";
    }
}

int supervisorctl_main(int argc, char* argv[]) {
    try {
        po::options_description desc("supervisorctl - control supervisord processes");
        desc.add_options()
            ("help,h", "Show this help message")
            ("version,v", "Show version information")
            ("config,c", po::value<std::string>()->default_value("/etc/supervisord.conf"),
             "Configuration file path")
        ;

        // Parse known options, leave rest for commands
        po::variables_map vm;
        auto parsed = po::command_line_parser(argc, argv)
            .options(desc)
            .allow_unregistered()
            .run();

        po::store(parsed, vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << std::endl;
            std::cout << "\nCommands:\n";
            std::cout << "  status [name]         Show process status\n";
            std::cout << "  start <name|all>      Start process(es)\n";
            std::cout << "  stop <name|all>       Stop process(es)\n";
            std::cout << "  restart <name|all>    Restart process(es)\n";
            std::cout << "  shutdown              Shutdown supervisord\n";
            std::cout << "  reload                Reload configuration\n";
            std::cout << "  help                  Show this help\n";
            std::cout << "  exit/quit             Exit supervisorctl\n";
            std::cout << "\nUsage:\n";
            std::cout << "  supervisorctl status           # Show all process status\n";
            std::cout << "  supervisorctl start myapp      # Start a specific process\n";
            std::cout << "  supervisorctl                  # Interactive mode\n";
            return 0;
        }

        if (vm.count("version")) {
            std::cout << "supervisorctl 0.1.0 (C++ minimal replacement)" << std::endl;
            return 0;
        }

        // Load config to get socket path
        std::string config_file = vm["config"].as<std::string>();
        supervisord::config::Configuration config;

        try {
            config = supervisord::config::ConfigParser::parse_file(config_file);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not load config file: " << e.what() << std::endl;
            std::cerr << "Using default socket path: /tmp/supervisor.sock" << std::endl;
            config.supervisorctl.serverurl = "unix:///tmp/supervisor.sock";
        }

        // Extract socket path from serverurl (unix:///path/to/socket)
        std::string serverurl = config.supervisorctl.serverurl;
        std::string socket_path;

        if (serverurl.find("unix://") == 0) {
            socket_path = serverurl.substr(7);
        } else {
            std::cerr << "Error: Only Unix socket URLs are supported" << std::endl;
            return 1;
        }

        // Create client
        SupervisorctlClient client(socket_path);

        // Get remaining arguments as command
        std::vector<std::string> command_args = po::collect_unrecognized(parsed.options, po::include_positional);

        if (command_args.empty()) {
            // No command specified, enter interactive mode
            interactive_mode(client);
        } else {
            // Execute single command and exit
            return execute_command(client, command_args);
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
