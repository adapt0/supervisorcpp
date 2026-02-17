#pragma once
#ifndef SUPERVISOR_LIB__RPC__RPC_FWD
#define SUPERVISOR_LIB__RPC__RPC_FWD

#include <memory>
#include <string>
#include <vector>

namespace supervisorcpp::rpc {

class RpcConnection;
using RpcConnectionPtr = std::shared_ptr<RpcConnection>;

class RpcServer;
using RpcServerPtr = std::shared_ptr<RpcServer>;
using RpcServerWeak = std::weak_ptr<RpcServer>;

using RpcParams = std::vector<std::string>;

} // namespace supervisorcpp::rpc

#endif // SUPERVISOR_LIB__RPC__RPC_SERVER
