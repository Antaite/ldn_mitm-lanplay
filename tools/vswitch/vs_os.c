/* SPDX-License-Identifier: GPL-2.0-only */
#include "vs_os.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

void vs_os_verbose(int on) {
#ifdef _WIN32
    _putenv(on ? "LP_QUIET=" : "LP_QUIET=1");
#else
    if (on) unsetenv("LP_QUIET"); else setenv("LP_QUIET", "1", 1);
#endif
}

void vs_os_init(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    srand((unsigned)(GetTickCount() ^ GetCurrentProcessId()));
#else
    srand((unsigned)(time(NULL) ^ (getpid() << 8)));
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    vs_os_verbose(0);
}
