#pragma once
#ifndef SUPERVISOR_APP__DAEMON_STATE
#define SUPERVISOR_APP__DAEMON_STATE

#include <string_view>

namespace supervisorcpp {

/// Daemon-level state codes (matches Python supervisord)
enum class DaemonState : int {
    SHUTDOWN    = -1,
    RESTARTING  =  0,
    RUNNING     =  1,
    FATAL       =  2,
};

constexpr std::string_view daemon_state_name(DaemonState state) {
    switch (state) {
    case DaemonState::FATAL:      return "FATAL";
    case DaemonState::RUNNING:    return "RUNNING";
    case DaemonState::RESTARTING: return "RESTARTING";
    case DaemonState::SHUTDOWN:   return "SHUTDOWN";
    }
    return "UNKNOWN";
}

} // namespace supervisorcpp

#endif // SUPERVISOR_APP__DAEMON_STATE
