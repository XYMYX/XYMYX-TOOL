// features.h - one entry function per menu item.
//
// Each function reads its data through src/platform/platform.h, prints the
// result and offers to save it. None of them contains #ifdef _WIN32: the
// features are the same on every operating system.

#pragma once

void runSystemInfo();
void runNetwork();
void runHashChecker();
void runSecurityCheck();
