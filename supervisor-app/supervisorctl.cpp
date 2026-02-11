#include "config/config_parser.h"
#include "rpc/rpc_fwd.h"
#include "rpc/xmlrpc.h"
#include "util/string.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

using RpcParams = supervisorcpp::rpc::RpcParams;

/**
 * XML-RPC client for supervisorctl
 */
class SupervisorCtlClient {
public:
    using CommandFunc = int (SupervisorCtlClient::*)(const RpcParams&);

    struct CommandInfo {
        std::string_view    method;
        std::string_view    desc_args;
        std::string_view    desc;
        CommandFunc         func;
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

    explicit SupervisorCtlClient(const std::string& socket_path)
    : socket_path_(socket_path)
    { }

    /**
     * Execute a supervisorctl command
     */
    int execute_command(const std::string& method, const RpcParams& args) {
        if (method.empty()) return 0;

        try {
            for (const auto& cmd : commands_) {
                if (cmd.method == method) {
                    return (this->*(cmd.func))(args);
                }
            }

            std::cerr << "*** Unknown command: " << method << std::endl;
            std::cerr << "Use 'help' for available commands" << std::endl;
            return 1;

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }

    int cmdHelp(const RpcParams&) {
        std::cout << "Commands:\n";
        for (const auto& cmd : commands_) {
            if (cmd.desc.empty()) continue;

            auto w = cmd.method.size() + cmd.desc_args.size();
            if (w < 22) w = 22 - cmd.method.size() - 1;
            std::cout << "  " << cmd.method
                << ' ' << std::left << std::setw(w) << cmd.desc_args
                << cmd.desc
                << '\n'
            ;
        }
        return 0;
    }

private:
    int cmdExit_(const RpcParams&) {
        return -1;  // Signal to exit
    }

    int cmdReload_(const RpcParams&) {
        std::cout << "Restarting supervisord..." << std::endl;
        call_method_("supervisor.shutdown");
        return 0;
    }

    int cmdRestart_(const RpcParams& args) {
        if (args.size() < 1) {
            std::cerr << "Error: process name required" << std::endl;
            return 1;
        }

        if (args[0] == "all") {
            call_method_("supervisor.stopAllProcesses");
            call_method_("supervisor.startAllProcesses");
            std::cout << "All processes restarted" << std::endl;
        } else {
            call_method_("supervisor.restart", {args[0]});
            std::cout << args[0] << ": restarted" << std::endl;
        }
        return 0;
    }

    int cmdShutdown_(const RpcParams&) {
        std::cout << "Shutting down supervisord" << std::endl;
        call_method_("supervisor.shutdown");
        return 0;
    }

    int cmdStart_(const RpcParams& args) {
        if (args.size() < 1) {
            std::cerr << "Error: process name required" << std::endl;
            return 1;
        }

        if (args[0] == "all") {
            call_method_("supervisor.startAllProcesses");
            std::cout << "All processes started" << std::endl;
        } else {
            call_method_("supervisor.startProcess", { args[0] });
            std::cout << args[0] << ": started" << std::endl;
        }
        return 0;
    }

    int cmdStop_(const RpcParams& args) {
        if (args.size() < 1) {
            std::cerr << "Error: process name required" << std::endl;
            return 1;
        }

        if (args[0] == "all") {
            call_method_("supervisor.stopAllProcesses");
            std::cout << "All processes stopped" << std::endl;
        } else {
            call_method_("supervisor.stopProcess", { args[0] });
            std::cout << args[0] << ": stopped" << std::endl;
        }
        return 0;
    }

    int cmdStatus_(const RpcParams&) {
        print_process_info_(
            parse_process_info_array_(
                call_method_("supervisor.getAllProcessInfo")
            )
        );
        return 0;
    }

    static constexpr CommandInfo commands_[] = {
        { "status",   "[name]",     "Show process status",  &SupervisorCtlClient::cmdStatus_ },
        { "start",    "<name|all>", "Start process(es)",    &SupervisorCtlClient::cmdStart_ },
        { "stop",     "<name|all>", "Stop process(es)",     &SupervisorCtlClient::cmdStop_ },
        { "restart",  "<name|all>", "Restart process(es)",  &SupervisorCtlClient::cmdRestart_ },
        { "shutdown", "",           "Shutdown supervisord", &SupervisorCtlClient::cmdShutdown_ },
        { "reload",   "",           "Reload configuration", &SupervisorCtlClient::cmdReload_ },
        { "help",     "",           "Show this help",       &SupervisorCtlClient::cmdHelp },
        { "exit",     "",           "Exit supervisorctl",   &SupervisorCtlClient::cmdExit_ },
        { "quit",     "",           "",                     &SupervisorCtlClient::cmdExit_ },
    };

    /**
     * Call RPC method
     * Creates a fresh socket per call so the client is reusable in interactive mode
     */
    std::string call_method_(const std::string& method_name, const RpcParams& params = {}) {
        using stream_protocol = boost::asio::local::stream_protocol;

        try {
            stream_protocol::socket socket(io_context_);
            socket.connect(
                stream_protocol::endpoint{socket_path_}
            );

            // Build XML-RPC request
            const auto request = build_xmlrpc_request_(method_name, params);
            const auto http_request = build_http_request_(request);

            // Send request
            boost::asio::write(socket, boost::asio::buffer(http_request));

            // Read response
            boost::asio::streambuf response;
            boost::asio::read_until(socket, response, "\r\n\r\n");

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
                        boost::asio::read(socket, boost::asio::buffer(&response_str[bytes_read], remaining));
                    }
                }
            }

            socket.close();

            return response_str;

        } catch (const std::exception& e) {
            throw std::runtime_error("RPC error: " + std::string(e.what()));
        }
    }

    /**
     * Format process info for display (supervisord-compatible format)
     */
    static void print_process_info_(const std::vector<ProcessInfo>& processes) {
        for (const auto& proc : processes) {
            std::cout << std::left << std::setw(20) << proc.name
                    << std::setw(15) << proc.statename
                    << proc.description << std::endl;
        }
    }

    static std::vector<ProcessInfo> parse_process_info_array_(const std::string& xml) {
        try {
            namespace pt = boost::property_tree;

            pt::ptree tree;
            {
                std::istringstream iss(xml);
                pt::read_xml(iss, tree);
            }

            // Navigate to the array
            const auto value_node = tree.get_child("methodResponse.params.param.value");

            // Check if it's an array
            std::vector<ProcessInfo> result;
            if (auto array_node = value_node.get_child_optional("array.data")) {
                for (const auto& item : *array_node) {
                    if (item.first == "value") {
                        ProcessInfo info;

                        // Parse struct members
                        if (const auto struct_node = item.second.get_child_optional("struct")) {
                            for (const auto& member : *struct_node) {
                                if (member.first == "member") {
                                    const auto name = member.second.get<std::string>("name");
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
            return result;

        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to parse response: " + std::string(e.what()));
        }
    }

    static std::string build_xmlrpc_request_(const std::string& method_name,
                                      const std::vector<std::string>& params) {
        namespace util   = supervisorcpp::util;
        namespace xmlrpc = supervisorcpp::xmlrpc;

        std::ostringstream oss;
        oss << "<?xml version=\"1.0\"?>\n";
        oss << "<methodCall>\n";
        oss << "  <methodName>" << util::escape_xml(method_name) << "</methodName>\n";
        oss << "  <params>\n";

        for (const auto& param : params) {
            oss << "    <param>" << xmlrpc::Value{param} << "</param>\n";
        }

        oss << "  </params>\n";
        oss << "</methodCall>\n";
        return oss.str();
    }

    static std::string build_http_request_(const std::string& body) {
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
};

/**
 * Interactive mode
 */
void interactive_mode(SupervisorCtlClient& client) {
    while (true) {
        std::cout << "supervisord> ";

        std::string line;
        if (!std::getline(std::cin, line)) break;

        boost::algorithm::trim(line);
        if (line.empty()) continue;

        // Split into words
        RpcParams args;
        boost::algorithm::split(
            args, line,
            [](char ch) { return std::isspace(ch); },
            boost::algorithm::token_compress_on
        );
        if (args.empty()) continue;

        const auto method = args.front();
        args.erase(std::begin(args));

        if (-1 == client.execute_command(method, args)) {
            break; // Exit requested
        }
    }
}

int supervisorctl_main(int argc, char* argv[]) {
    try {
        namespace po = boost::program_options;

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
            SupervisorCtlClient{""}.cmdHelp(RpcParams{});
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

        supervisorcpp::config::Configuration config;
        try {
            // Load config to get socket path
            const auto config_file = vm["config"].as<std::string>();
            config = supervisorcpp::config::ConfigParser::parse_file(config_file);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not load config file: " << e.what() << std::endl;
            std::cerr << "Using default socket path: /tmp/supervisor.sock" << std::endl;
            config.supervisorctl.serverurl = "unix:///tmp/supervisor.sock";
        }

        // Extract socket path from serverurl (unix:///path/to/socket)
        const auto& serverurl = config.supervisorctl.serverurl;
        std::string socket_path;
        if (serverurl.find("unix://") == 0) {
            socket_path = serverurl.substr(7);
        } else {
            std::cerr << "Error: Only Unix socket URLs are supported" << std::endl;
            return 1;
        }

        // Create client
        SupervisorCtlClient client{socket_path};

        // Get remaining arguments as command
        auto args = po::collect_unrecognized(parsed.options, po::include_positional);
        if (args.empty()) {
            // No command specified, enter interactive mode
            interactive_mode(client);
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
