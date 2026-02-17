// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#pragma once
#ifndef SUPERVISOR_LIB__UTIL__PLATFORM
#define SUPERVISOR_LIB__UTIL__PLATFORM

#ifndef _GNU_SOURCE
int pipe2(int pipefd[2], int flags);
#endif // _GNU_SOURCE

#endif // SUPERVISOR_LIB__UTIL__PLATFORM
