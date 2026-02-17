#include "args_parser.h"
#include "version.h"
#include "config/ini_reader.h"
#include "logger/logger.h"
#include "rpc/rpc_fwd.h"
#include "rpc/xmlrpc.h"
#include "util/string.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace supervisorcpp {

using ptree = boost::property_tree::ptree;
using RpcParams = supervisorcpp::rpc::RpcParams;

class XmlRpcError : public std::runtime_error {
public:
    XmlRpcError(std::string_view code, std::string_view name, std::string message)
    : std::runtime_error(std::move(message)), code_{code}, name_{name} { }

    std::string_view code() const { return code_; }
    std::string_view name() const { return name_; }

private:
    std::string code_;
    std::string name_;
};

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
        static ProcessInfo from(const ptree& pt) {
            const auto pt_struct_opt = pt.get_child_optional("struct");
            if (!pt_struct_opt) throw std::runtime_error("ProcessInfo expected struct");

            ProcessInfo info;
            for (const auto& member : *pt_struct_opt) {
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
            return info;
        }

        std::string name;
        std::string statename;
        int pid;
        std::string description;
    };

    explicit SupervisorCtlClient(const std::string& socket_path)
    : socket_path_(socket_path)
    { }

    ~SupervisorCtlClient() = default;

    SupervisorCtlClient(const SupervisorCtlClient&) = delete;
    SupervisorCtlClient& operator=(const SupervisorCtlClient&) = delete;
    SupervisorCtlClient(SupervisorCtlClient&&) = delete;
    SupervisorCtlClient& operator=(SupervisorCtlClient&&) = delete;

    /**
     * Interactive mode
     */
    void interactive_mode() {
        while (true) {
            std::cout << "supervisor> ";

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

            if (-1 == execute_command(method, args)) {
                break; // Exit requested
            }
        }
        std::cout << std::endl;
    }

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

            std::cerr << "Error: unknown command '" << method << "'\n";
            std::cerr << "Use 'help' for available commands\n";
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
            // Stop first, but tolerate NOT_RUNNING (process may already be stopped)
            try {
                call_method_("supervisor.stopProcess", {args[0]});
            } catch (const XmlRpcError& e) {
                if (e.code() != "NOT_RUNNING") throw;
            }
            call_method_("supervisor.startProcess", {args[0]});
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

    int cmdStatus_(const RpcParams& args) {
        if (args.size() < 1 || args[0] == "all") {
            print_process_info_(
                parse_process_info_array_(
                    call_method_("supervisor.getAllProcessInfo")
                )
            );
        } else {
            print_process_info_(
                parse_process_info_(
                    call_method_("supervisor.getProcessInfo", { args[0] })
                )
            );
        }
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
    ptree call_method_(const std::string& method_name, const RpcParams& params = {}) {
        using stream_protocol = boost::asio::local::stream_protocol;

        try {
            stream_protocol::socket socket(io_context_);
            socket.connect(
                stream_protocol::endpoint{socket_path_}
            );

            // Build XML-RPC request
            const auto request = build_xmlrpc_request_(method_name, params);
            LOG_TRACE << "<- " << request;
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

            LOG_TRACE << "-> " << response_str;

            // process response
            ptree tree;
            {
                std::istringstream iss{response_str};
                boost::property_tree::read_xml(iss, tree);
            }
            check_fault_(tree);
            return tree;

        } catch (const XmlRpcError&) {
            throw;
        } catch (const boost::system::system_error& e) {
            if (e.code() == boost::asio::error::connection_refused) {
                throw std::runtime_error("Connection refused — is supervisord running?");
            }
            if (e.code().category() == boost::system::system_category()) {
                throw std::runtime_error("Connection failed: " + std::string(e.code().message())
                    + " (" + socket_path_ + ")");
            }
            throw std::runtime_error("RPC error: " + std::string(e.code().message()));
        } catch (const std::exception& e) {
            throw std::runtime_error("RPC error: " + std::string(e.what()));
        }
    }

    /**
     * Format process info for display (supervisord-compatible format)
     */
    static void print_process_info_(const std::vector<ProcessInfo>& processes) {
        for (const auto& info : processes) {
            print_process_info_(info);
        }
    }
    static void print_process_info_(const ProcessInfo& info) {
        std::cout << std::left << std::setw(20) << info.name
                << std::setw(15) << info.statename
                << info.description << std::endl;
    }

    static std::vector<ProcessInfo> parse_process_info_array_(const ptree& tree) {
        try {
            const auto& value_node = tree.get_child("methodResponse.params.param.value");

            std::vector<ProcessInfo> result;
            if (const auto array_node = value_node.get_child_optional("array.data")) {
                for (const auto& item : *array_node) {
                    if (item.first == "value") {
                        result.push_back(
                            ProcessInfo::from(item.second)
                        );
                    }
                }
            }
            return result;

        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to parse response: " + std::string(e.what()));
        }
    }

    static ProcessInfo parse_process_info_(const ptree& tree) {
        try {
            return ProcessInfo::from(
                tree.get_child("methodResponse.params.param.value")
            );

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
        oss << "  <params>";
        for (const auto& param : params) {
            oss << "<param><value>" << xmlrpc::Value{param} << "</value></param>";
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

    static void check_fault_(const ptree& tree) {
        const auto fault_struct = tree.get_child_optional("methodResponse.fault.value.struct");
        if (!fault_struct) return;

        std::string fault;
        for (const auto& member : *fault_struct) {
            if (member.first != "member") continue;
            if (member.second.get<std::string>("name", "") != "faultString") continue;
            fault = member.second.get<std::string>("value.string", "");
            break;
        }
        if (fault.empty()) fault = "Unknown RPC fault";

        // Translate "CODE: name" faults to friendly XmlRpcError
        static constexpr std::pair<std::string_view, std::string_view> translations[] = {
            { "BAD_NAME",        "no such process" },
            { "NOT_RUNNING",     "not running" },
            { "ALREADY_STARTED", "already running" },
            { "SPAWN_ERROR",     "spawn error" },
        };

        for (const auto& [code, friendly] : translations) {
            if (const auto sep = std::string{code} + ": "; fault.starts_with(sep)) {
                const auto name = fault.substr(sep.size());
                throw XmlRpcError(code, name, name + " - " + std::string{friendly});
            }
        }

        throw std::runtime_error(fault);
    }

    std::string socket_path_;
    boost::asio::io_context io_context_;
};

} // namespace supervisorcpp

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
                SupervisorCtlClient{""}.cmdHelp(supervisorcpp::RpcParams{});
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
