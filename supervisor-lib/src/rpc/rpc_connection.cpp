#include "rpc_connection.h"
#include "xmlrpc.h"
#include "../logger/logger.h"
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace supervisorcpp::rpc {

namespace pt = boost::property_tree;

RpcConnection::RpcConnection(boost::asio::io_context& io_context, DispatchFunction dispatcher)
: socket_(io_context)
, dispatcher_(std::move(dispatcher))
{ }

RpcConnection::~RpcConnection() = default;

void RpcConnection::start() {
    begin_read_();
}

void RpcConnection::begin_read_() {
    socket_.async_read_some(boost::asio::buffer(buffer_), [
        self = shared_from_this()
    ](const boost::system::error_code& error, size_t bytes) {
        self->handle_read_(error, bytes);
    });
}

void RpcConnection::handle_read_(const boost::system::error_code& error, size_t bytes_transferred) {
    const bool got_eof = (error == boost::asio::error::eof);
    if (error && !got_eof) return;

    // SECURITY/ROBUSTNESS: Limit maximum request size to 1MB to prevent memory exhaustion
    constexpr size_t MAX_REQUEST_SIZE = 1024 * 1024;  // 1MB

    if (request_data_.size() + bytes_transferred > MAX_REQUEST_SIZE) {
        LOG_WARN << "RPC request too large (" << (request_data_.size() + bytes_transferred)
                 << " bytes), rejecting. Max size: " << MAX_REQUEST_SIZE;

        send_response_(
            generate_xmlrpc_fault_(1, "Request too large (max 1MB)")
        );
        return;
    }

    request_data_.append(buffer_.data(), bytes_transferred);

    // Find end of HTTP headers
    size_t header_end = request_data_.find("\r\n\r\n");
    size_t body_offset = (header_end != std::string::npos) ? header_end + 4 : std::string::npos;

    if (body_offset == std::string::npos) {
        header_end = request_data_.find("\n\n");
        body_offset = (header_end != std::string::npos) ? header_end + 2 : std::string::npos;
    }

    if (body_offset == std::string::npos) {
        if (!got_eof) begin_read_();
        return;
    }

    // Parse Content-Length to determine body size (case-insensitive per RFC 7230).
    // Without Content-Length: body length is zero per RFC 7230 §3.3.3.
    const auto headers = std::string_view{request_data_}.substr(0, header_end);
    const auto cl_range = boost::ifind_first(headers, "content-length:");

    size_t content_length = 0;
    if (!cl_range.empty()) {
        const auto after_name = static_cast<size_t>(cl_range.end() - headers.begin());
        const auto val_start = headers.find_first_not_of(" \t", after_name);
        if (val_start != std::string_view::npos) {
            content_length = std::stoull(std::string{headers.substr(val_start)});
        }
        if (request_data_.size() - body_offset < content_length) {
            if (!got_eof) begin_read_();
            return;
        }
    }

    // Trim to exactly headers + content_length bytes
    request_data_.resize(body_offset + content_length);
    process_request_(request_data_);
}

void RpcConnection::process_request_(const std::string& request) {
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

        const auto xml_body = request.substr(body_start);

        // Parse XML-RPC request
        const auto [method_name, params] = parse_xmlrpc_request_(xml_body);

        LOG_DEBUG << "RPC call: " << method_name;

        // Dispatch method
        if (!dispatcher_) throw std::runtime_error("No method dispatcher");
        const auto result = dispatcher_(method_name, params);

        send_response_(
            generate_xmlrpc_response_(result)
        );

    } catch (const std::exception& e) {
        LOG_ERROR << "RPC error: " << e.what();

        send_response_(
            generate_xmlrpc_fault_(1, e.what())
        );
    }
}

void RpcConnection::send_response_(const std::string& body) {
    const auto data_ptr = std::make_shared<std::string>(generate_http_response_(body));
    boost::asio::async_write(socket_, boost::asio::buffer(*data_ptr), [
        self_ptr{shared_from_this()}, data_ptr
    ](const boost::system::error_code& error, size_t) {
        self_ptr->handle_write_(error);
    });
}

void RpcConnection::handle_write_(const boost::system::error_code& /*error*/) {
    // Close connection after response
    boost::system::error_code ec;
    socket_.close(ec);
}

std::pair<std::string, RpcParams> RpcConnection::parse_xmlrpc_request_(const std::string& xml) {
    try {
        pt::ptree tree;
        {
            std::istringstream iss(xml);
            pt::read_xml(iss, tree);
        }

        const auto method_name = tree.get<std::string>("methodCall.methodName");

        // Parse params (simplified - only handles string params)
        RpcParams params;
        if (const auto params_tree = tree.get_child_optional("methodCall.params")) {
            for (const auto& param : *params_tree) {
                if (param.first == "param") {
                    // Try to get string value
                    if (const auto str_val = param.second.get_optional<std::string>("value.string")) {
                        params.push_back(*str_val);
                    } else if (const auto int_val = param.second.get_optional<int>("value.int")) {
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

std::string RpcConnection::generate_xmlrpc_response_(const std::string& result) {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\"?>\n"
        << "<methodResponse>\n"
        << "  <params>\n"
        << "    <param><value>" << result << "</value></param>\n"
        << "  </params>\n"
        << "</methodResponse>\n"
    ;
    return oss.str();
}

std::string RpcConnection::generate_xmlrpc_fault_(int code, const std::string& message) {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\"?>\n"
        << "<methodResponse>\n"
        << "  <fault>\n"
        << "    <value>"
        << xmlrpc::Struct{
            xmlrpc::Member{"faultCode", code},
            xmlrpc::Member{"faultString", message},
        } << "</value>\n"
        << "  </fault>\n"
        << "</methodResponse>\n"
    ;
    return oss.str();
}

std::string RpcConnection::generate_http_response_(const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n";
    oss << "Content-Type: text/xml\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

} // namespace supervisorcpp::rpc
