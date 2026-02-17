// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Chris Byrne

#include "platform.h"
#include <fcntl.h>
#include <unistd.h>

#ifndef _GNU_SOURCE
int pipe2(int pipefd[2], int flags) {
    const int res = pipe(pipefd);
    if (0 != res) return res;

    fcntl(pipefd[0], F_SETFD, fcntl(pipefd[0], F_GETFD) | flags);
    fcntl(pipefd[1], F_SETFD, fcntl(pipefd[1], F_GETFD) | flags);
    return res;
}
#endif // _GNU_SOURCE
