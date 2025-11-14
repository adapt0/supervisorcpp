#include "rpc_server.h"
#include "../util/logger.h"
#include "../security.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>

namespace supervisord {
namespace rpc {

namespace pt = boost::property_tree;

// ============================================================================
// RpcServer implementation
// ============================================================================

RpcServer::RpcServer(boost::asio::io_context& io_context,
                     const std::string& socket_path,
                     process::ProcessManager& process_manager)
    : io_context_(io_context)
    , acceptor_(io_context)
    , socket_path_(socket_path)
    , process_manager_(process_manager)
    , running_(false)
{
    register_handlers();
}

RpcServer::~RpcServer() {
    stop();
}

void RpcServer::start() {
    // Remove existing socket file if it exists
    ::unlink(socket_path_.c_str());

    // Create endpoint
    boost::asio::local::stream_protocol::endpoint endpoint(socket_path_);

    // Open and bind acceptor
    acceptor_.open(endpoint.protocol());
    acceptor_.bind(endpoint);
    acceptor_.listen();

    // SECURITY: Set socket permissions to 0600 (owner read/write only)
    try {
        security::set_socket_permissions(socket_path_);
    } catch (const security::SecurityError& e) {
        LOG_ERROR << "Failed to set socket permissions: " << e.what();
        throw;
    }

    running_ = true;
    LOG_INFO << "RPC server listening on " << socket_path_;

    start_accept();
}

void RpcServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    acceptor_.close();
    ::unlink(socket_path_.c_str());

    LOG_INFO << "RPC server stopped";
}

void RpcServer::start_accept() {
    auto connection = std::make_shared<RpcConnection>(io_context_, *this);

    acceptor_.async_accept(connection->socket(),
        [this, connection](const boost::system::error_code& error) {
            handle_accept(connection, error);
        });
}

void RpcServer::handle_accept(std::shared_ptr<RpcConnection> connection,
                              const boost::system::error_code& error) {
    if (!error) {
        LOG_DEBUG << "RPC connection accepted";
        connection->start();
    } else {
        LOG_ERROR << "RPC accept error: " << error.message();
    }

    if (running_) {
        start_accept();
    }
}

void RpcServer::register_handlers() {
    handlers_["supervisor.getState"] = [this](auto& p) { return handle_get_state(p); };
    handlers_["supervisor.getAllProcessInfo"] = [this](auto& p) { return handle_get_all_process_info(p); };
    handlers_["supervisor.getProcessInfo"] = [this](auto& p) { return handle_get_process_info(p); };
    handlers_["supervisor.startProcess"] = [this](auto& p) { return handle_start_process(p); };
    handlers_["supervisor.stopProcess"] = [this](auto& p) { return handle_stop_process(p); };
    handlers_["supervisor.startAllProcesses"] = [this](auto& p) { return handle_start_all_processes(p); };
    handlers_["supervisor.stopAllProcesses"] = [this](auto& p) { return handle_stop_all_processes(p); };
    handlers_["supervisor.restart"] = [this](auto& p) { return handle_restart_process(p); };
    handlers_["supervisor.shutdown"] = [this](auto& p) { return handle_shutdown(p); };
}

std::string RpcServer::dispatch_method(const std::string& method_name,
                                       const std::vector<std::string>& params) {
    auto it = handlers_.find(method_name);
    if (it == handlers_.end()) {
        throw std::runtime_error("Unknown method: " + method_name);
    }

    return it->second(params);
}

std::string RpcServer::handle_get_state(const std::vector<std::string>& /*params*/) {
    // Return supervisor state
    // States: RUNNING=1, others not implemented in minimal version
    return "<struct>"
           "<member><name>statecode</name><value><int>1</int></value></member>"
           "<member><name>statename</name><value><string>RUNNING</string></value></member>"
           "</struct>";
}

std::string RpcServer::handle_get_all_process_info(const std::vector<std::string>& /*params*/) {
    auto infos = process_manager_.get_all_process_info();

    std::ostringstream oss;
    oss << "<array><data>";

    for (const auto& info : infos) {
        oss << "<value><struct>";
        oss << "<member><name>name</name><value><string>" << info.name << "</string></value></member>";
        oss << "<member><name>group</name><value><string>" << info.group << "</string></value></member>";
        oss << "<member><name>statename</name><value><string>";

        switch (info.state) {
            case config::ProcessState::STOPPED: oss << "STOPPED"; break;
            case config::ProcessState::STARTING: oss << "STARTING"; break;
            case config::ProcessState::RUNNING: oss << "RUNNING"; break;
            case config::ProcessState::BACKOFF: oss << "BACKOFF"; break;
            case config::ProcessState::STOPPING: oss << "STOPPING"; break;
            case config::ProcessState::EXITED: oss << "EXITED"; break;
            case config::ProcessState::FATAL: oss << "FATAL"; break;
            default: oss << "UNKNOWN";
        }

        oss << "</string></value></member>";
        oss << "<member><name>state</name><value><int>" << info.state_code << "</int></value></member>";
        oss << "<member><name>pid</name><value><int>" << info.pid << "</int></value></member>";
        oss << "<member><name>exitstatus</name><value><int>" << info.exitstatus << "</int></value></member>";
        oss << "<member><name>stdout_logfile</name><value><string>" << info.stdout_logfile << "</string></value></member>";
        oss << "<member><name>spawnerr</name><value><string>" << info.spawnerr << "</string></value></member>";
        oss << "<member><name>description</name><value><string>" << info.description << "</string></value></member>";
        oss << "</struct></value>";
    }

    oss << "</data></array>";
    return oss.str();
}

std::string RpcServer::handle_get_process_info(const std::vector<std::string>& params) {
    if (params.empty()) {
        throw std::runtime_error("Process name required");
    }

    auto* proc = process_manager_.get_process(params[0]);
    if (!proc) {
        throw std::runtime_error("Process not found: " + params[0]);
    }

    auto info = proc->get_info();

    std::ostringstream oss;
    oss << "<struct>";
    oss << "<member><name>name</name><value><string>" << info.name << "</string></value></member>";
    oss << "<member><name>group</name><value><string>" << info.group << "</string></value></member>";
    oss << "<member><name>statename</name><value><string>";

    switch (info.state) {
        case config::ProcessState::STOPPED: oss << "STOPPED"; break;
        case config::ProcessState::STARTING: oss << "STARTING"; break;
        case config::ProcessState::RUNNING: oss << "RUNNING"; break;
        case config::ProcessState::BACKOFF: oss << "BACKOFF"; break;
        case config::ProcessState::STOPPING: oss << "STOPPING"; break;
        case config::ProcessState::EXITED: oss << "EXITED"; break;
        case config::ProcessState::FATAL: oss << "FATAL"; break;
        default: oss << "UNKNOWN";
    }

    oss << "</string></value></member>";
    oss << "<member><name>state</name><value><int>" << info.state_code << "</int></value></member>";
    oss << "<member><name>pid</name><value><int>" << info.pid << "</int></value></member>";
    oss << "</struct>";

    return oss.str();
}

std::string RpcServer::handle_start_process(const std::vector<std::string>& params) {
    if (params.empty()) {
        throw std::runtime_error("Process name required");
    }

    bool result = process_manager_.start_process(params[0]);
    return result ? "<boolean>1</boolean>" : "<boolean>0</boolean>";
}

std::string RpcServer::handle_stop_process(const std::vector<std::string>& params) {
    if (params.empty()) {
        throw std::runtime_error("Process name required");
    }

    bool result = process_manager_.stop_process(params[0]);
    return result ? "<boolean>1</boolean>" : "<boolean>0</boolean>";
}

std::string RpcServer::handle_start_all_processes(const std::vector<std::string>& /*params*/) {
    process_manager_.start_all();

    // Return array of process infos
    return handle_get_all_process_info({});
}

std::string RpcServer::handle_stop_all_processes(const std::vector<std::string>& /*params*/) {
    process_manager_.stop_all();

    // Return array of process infos
    return handle_get_all_process_info({});
}

std::string RpcServer::handle_restart_process(const std::vector<std::string>& params) {
    if (params.empty()) {
        throw std::runtime_error("Process name required");
    }

    bool result = process_manager_.restart_process(params[0]);
    return result ? "<boolean>1</boolean>" : "<boolean>0</boolean>";
}

std::string RpcServer::handle_shutdown(const std::vector<std::string>& /*params*/) {
    LOG_INFO << "Shutdown requested via RPC";

    // Stop the IO context to trigger shutdown
    io_context_.post([this]() {
        process_manager_.stop_all();
        io_context_.stop();
    });

    return "<boolean>1</boolean>";
}

// ============================================================================
// RpcConnection implementation
// ============================================================================

RpcConnection::RpcConnection(boost::asio::io_context& io_context, RpcServer& server)
    : socket_(io_context)
    , server_(server)
{
}

void RpcConnection::start() {
    socket_.async_read_some(boost::asio::buffer(buffer_),
        [self = shared_from_this()](const boost::system::error_code& error, size_t bytes) {
            self->handle_read(error, bytes);
        });
}

void RpcConnection::handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
        return;  // Connection closed or error
    }

    // SECURITY/ROBUSTNESS: Limit maximum request size to 1MB to prevent memory exhaustion
    constexpr size_t MAX_REQUEST_SIZE = 1024 * 1024;  // 1MB

    if (request_data_.size() + bytes_transferred > MAX_REQUEST_SIZE) {
        LOG_WARN << "RPC request too large (" << (request_data_.size() + bytes_transferred)
                 << " bytes), rejecting. Max size: " << MAX_REQUEST_SIZE;

        // Send fault response and close connection
        std::string fault = generate_xmlrpc_fault(1, "Request too large (max 1MB)");
        std::string response = generate_http_response(fault);
        boost::asio::async_write(socket_, boost::asio::buffer(response),
            [self = shared_from_this()](const boost::system::error_code&, size_t) {
                self->socket_.close();
            });
        return;
    }

    request_data_.append(buffer_.data(), bytes_transferred);

    // Check if we have complete HTTP request (ends with \r\n\r\n or \n\n)
    if (request_data_.find("\r\n\r\n") != std::string::npos ||
        request_data_.find("\n\n") != std::string::npos) {

        process_request(request_data_);
    } else {
        // Continue reading
        socket_.async_read_some(boost::asio::buffer(buffer_),
            [self = shared_from_this()](const boost::system::error_code& error, size_t bytes) {
                self->handle_read(error, bytes);
            });
    }
}

void RpcConnection::process_request(const std::string& request) {
    try {
        // Extract XML body from HTTP POST request
        size_t body_start = request.find("\r\n\r\n");
        if (body_start == std::string::npos) {
            body_start = request.find("\n\n");
            if (body_start != std::string::npos) {
                body_start += 2;
            }
        } else {
            body_start += 4;
        }

        if (body_start == std::string::npos) {
            throw std::runtime_error("Invalid HTTP request");
        }

        std::string xml_body = request.substr(body_start);

        // Parse XML-RPC request
        auto [method_name, params] = parse_xmlrpc_request(xml_body);

        LOG_DEBUG << "RPC call: " << method_name;

        // Dispatch method
        std::string result = server_.dispatch_method(method_name, params);

        // Generate XML-RPC response
        std::string xml_response = generate_xmlrpc_response(result);

        // Generate HTTP response
        std::string http_response = generate_http_response(xml_response);

        // Send response
        boost::asio::async_write(socket_, boost::asio::buffer(http_response),
            [self = shared_from_this()](const boost::system::error_code& error, size_t /*bytes*/) {
                self->handle_write(error);
            });

    } catch (const std::exception& e) {
        LOG_ERROR << "RPC error: " << e.what();

        // Send fault response
        std::string fault = generate_xmlrpc_fault(1, e.what());
        std::string http_response = generate_http_response(fault);

        boost::asio::async_write(socket_, boost::asio::buffer(http_response),
            [self = shared_from_this()](const boost::system::error_code& error, size_t /*bytes*/) {
                self->handle_write(error);
            });
    }
}

void RpcConnection::handle_write(const boost::system::error_code& /*error*/) {
    // Close connection after response
    boost::system::error_code ec;
    socket_.close(ec);
}

std::pair<std::string, std::vector<std::string>> RpcConnection::parse_xmlrpc_request(const std::string& xml) {
    try {
        std::istringstream iss(xml);
        pt::ptree tree;
        pt::read_xml(iss, tree);

        std::string method_name = tree.get<std::string>("methodCall.methodName");
        std::vector<std::string> params;

        // Parse params (simplified - only handles string params)
        if (auto params_tree = tree.get_child_optional("methodCall.params")) {
            for (const auto& param : *params_tree) {
                if (param.first == "param") {
                    // Try to get string value
                    if (auto str_val = param.second.get_optional<std::string>("value.string")) {
                        params.push_back(*str_val);
                    } else if (auto int_val = param.second.get_optional<int>("value.int")) {
                        params.push_back(std::to_string(*int_val));
                    }
                }
            }
        }

        return {method_name, params};

    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse XML-RPC request: " + std::string(e.what()));
    }
}

std::string RpcConnection::generate_xmlrpc_response(const std::string& result) {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\"?>\n";
    oss << "<methodResponse>\n";
    oss << "  <params>\n";
    oss << "    <param>\n";
    oss << "      <value>" << result << "</value>\n";
    oss << "    </param>\n";
    oss << "  </params>\n";
    oss << "</methodResponse>\n";
    return oss.str();
}

std::string RpcConnection::generate_xmlrpc_fault(int code, const std::string& message) {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\"?>\n";
    oss << "<methodResponse>\n";
    oss << "  <fault>\n";
    oss << "    <value>\n";
    oss << "      <struct>\n";
    oss << "        <member>\n";
    oss << "          <name>faultCode</name>\n";
    oss << "          <value><int>" << code << "</int></value>\n";
    oss << "        </member>\n";
    oss << "        <member>\n";
    oss << "          <name>faultString</name>\n";
    oss << "          <value><string>" << message << "</string></value>\n";
    oss << "        </member>\n";
    oss << "      </struct>\n";
    oss << "    </value>\n";
    oss << "  </fault>\n";
    oss << "</methodResponse>\n";
    return oss.str();
}

std::string RpcConnection::generate_http_response(const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n";
    oss << "Content-Type: text/xml\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

} // namespace rpc
} // namespace supervisord
