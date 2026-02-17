#include "supervisord.h"
#include "args_parser.h"
#include "version.h"
#include "config/config_parser.h"
#include "logger/logger.h"
#include "rpc/xmlrpc.h"
#include "util/pidfile.h"
#include <csignal>
#include <boost/program_options.hpp>

namespace supervisorcpp {

Supervisord::Supervisord(const config::Configuration& config)
: config_{config}
, process_manager_{io_context_}
, rpc_server_ptr_{rpc::RpcServer::create(io_context_, config_.unix_http_server.socket_file.string())}
{
    for (const auto& prog : config_.programs) {
        process_manager_.add_process(prog);
    }
}

int Supervisord::run() {
    // Setup signal handlers for SIGTERM and SIGINT
    boost::asio::signal_set signals(io_context_, SIGTERM, SIGINT);
    signals.async_wait([this](const boost::system::error_code& error, int signal_number) {
        if (!error) {
            LOG_INFO << "Received shutdown signal (" << signal_number << ")";
            daemon_state_.store(DaemonState::SHUTDOWN);
            process_manager_.stop_all();
            io_context_.stop();
        }
    });

    // Ignore SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);

    // Register RPC handlers and start server
    register_rpc_handlers_();
    rpc_server_ptr_->start();

    LOG_INFO << "Supervising " << config_.programs.size() << " program(s)";
    for (const auto& prog : config_.programs) {
        LOG_INFO << "- " << prog.name << ": " << prog.command;
    }

    // Start all processes
    process_manager_.start_all();

    // Run event loop
    io_context_.run();

    // Cleanup
    rpc_server_ptr_->stop();
    LOG_INFO << "supervisord shutting down";

    return 0;
}

void Supervisord::register_rpc_handlers_() {
    using rpc::RpcParams;
    using namespace xmlrpc;

    rpc_server_ptr_->register_handler("supervisor.getState", [this](const RpcParams&) {
        const auto state = daemon_state_.load();
        return Struct{
            Member{"statecode", static_cast<int>(state)},
            Member{"statename", daemon_state_name(state)},
        }.str();
    });

    rpc_server_ptr_->register_handler("supervisor.getAllProcessInfo", [this](const RpcParams&) {
        return wrap( process_manager_.get_all_process_info() ).str();
    });

    rpc_server_ptr_->register_handler("supervisor.getProcessInfo", [this](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto* proc = process_manager_.get_process(params[0]);
        if (!proc) throw std::runtime_error("Process not found: " + params[0]);

        return wrap( proc->get_info() ).str();
    });

    rpc_server_ptr_->register_handler("supervisor.startProcess", [this](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto& name = params[0];
        const auto* proc = process_manager_.get_process(name);
        if (!proc) throw std::runtime_error("BAD_NAME: " + name);
        const auto st = proc->state();
        if (st == process::State::RUNNING || st == process::State::STARTING) {
            throw std::runtime_error("ALREADY_STARTED: " + name);
        }
        if (!process_manager_.start_process(name)) {
            throw std::runtime_error("SPAWN_ERROR: " + name);
        }
        return Value{true}.str();
    });

    rpc_server_ptr_->register_handler("supervisor.stopProcess", [this](const RpcParams& params) {
        if (params.empty()) throw std::runtime_error("Process name required");
        const auto& name = params[0];
        const auto* proc = process_manager_.get_process(name);
        if (!proc) throw std::runtime_error("BAD_NAME: " + name);
        const auto st = proc->state();
        if (st == process::State::STOPPED || st == process::State::EXITED || st == process::State::FATAL) {
            throw std::runtime_error("NOT_RUNNING: " + name);
        }
        if (!process_manager_.stop_process(name)) {
            throw std::runtime_error("NOT_RUNNING: " + name);
        }
        return Value{true}.str();
    });

    rpc_server_ptr_->register_handler("supervisor.startAllProcesses", [this](const RpcParams&) {
        process_manager_.start_all();
        return wrap( process_manager_.get_all_process_info() ).str();
    });

    rpc_server_ptr_->register_handler("supervisor.stopAllProcesses", [this](const RpcParams&) {
        process_manager_.stop_all();
        return wrap( process_manager_.get_all_process_info() ).str();
    });

    rpc_server_ptr_->register_handler("supervisor.shutdown", [this](const RpcParams&) {
        boost::asio::post(io_context_, [this]() {
            process_manager_.stop_all();
            io_context_.stop();
        });
        return Value{true}.str();
    });

    // Compatibility stubs for Python supervisorctl handshake
    const auto api_version = [](const RpcParams&) -> std::string {
        return "<string>3.0</string>";
    };
    rpc_server_ptr_->register_handler("supervisor.getVersion", api_version);
    rpc_server_ptr_->register_handler("supervisor.getSupervisorVersion", api_version);
    rpc_server_ptr_->register_handler("supervisor.getAPIVersion", api_version);

    rpc_server_ptr_->register_handler("supervisor.getIdentification", [](const RpcParams&) -> std::string {
        return "<string>supervisor</string>";
    });

    rpc_server_ptr_->register_handler("supervisor.getPID", [](const RpcParams&) -> std::string {
        return "<int>" + std::to_string(getpid()) + "</int>";
    });
}

} // namespace supervisorcpp


int supervisord_main(int argc, char* argv[]) {
    using namespace supervisorcpp;

    try {
        namespace po = boost::program_options;

        const auto parsed_opt = parse_args(
            argc, argv,
            "supervisord - process control system",
            [](auto& parser, auto& desc) {
                (void)parser;
                desc.add_options()
                    ("nodaemon,n", "For compatibility - always runs in foreground")
                    ("pidfile,p", po::value<std::string>(), "Path to pid file")
                ;
            }
        );
        if (!parsed_opt) return 0;
        const auto& [ vm, args ] = *parsed_opt;

        LOG_INFO << VERSION_STR;

        const auto config_file = vm["configuration"].as<std::string>();
        LOG_INFO << "Config: " << config_file;
        const auto config = config::ConfigParser::parse_file(config_file);

        LOG_INFO << "Log: " << config.supervisord.logfile.string();
        logger::init_file_logging(
            config.supervisord.logfile,
            config.supervisord.loglevel,
            config.supervisord.logfile_maxbytes,
            config.supervisord.logfile_backups,
            VERSION_STR
        );

        // Create pidfile guard (command-line overrides config file)
        const auto pidfile_guard = [&config, &vm]() -> std::optional<util::PidFileGuard> {
            if (vm.count("pidfile")) return util::PidFileGuard{vm["pidfile"].as<std::string>()};
            if (!config.supervisord.pidfile.empty()) return util::PidFileGuard{config.supervisord.pidfile};
            return std::optional<util::PidFileGuard>{};
        }();

        const int rc = Supervisord{config}.run();

        logger::shutdown_logging();
        return rc;

    } catch (const config::ConfigParseError& e) {
        LOG_ERROR << "Configuration error: " << e.what();
        return 1;
    } catch (const std::exception& e) {
        LOG_ERROR << "Error: " << e.what();
        return 1;
    }
}
