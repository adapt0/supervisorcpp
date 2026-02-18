// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__PROCESS__PROCESS_FWD
#define SUPERVISOR_LIB__PROCESS__PROCESS_FWD

#include <memory>

namespace supervisorcpp::process {

class ProcessManager;

class Process;
using ProcessPtr = std::shared_ptr<Process>;
using ProcessConstPtr = std::shared_ptr<const Process>;
using ProcessWeak = std::weak_ptr<Process>;

struct ProcessInfo;

} // namespace supervisorcpp::process

#endif // SUPERVISOR_LIB__PROCESS__PROCESS_FWD
