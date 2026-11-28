#pragma once

#include <cxxabi.h>
#include <unwind.h>
#include <array>
#include <dlfcn.h>
#include <signal.h>

extern uintptr_t g_sampLibBase;
extern uintptr_t g_gtasaLibBase;

void crash_log(const char* format, ...);
void dump_register(int signal, siginfo_t* info, void* ctx);
void dump_stack(int ignoreDepth = 0, int maxDepth = 31);
// Async-signal-safe crash breadcrumb: raw syscalls + __android_log_write only.
// Safe to call from a fatal-signal handler; dump_register/dump_stack are NOT.
// Writes a full crash report (signal/fault/pc/lr module offsets, registers,
// breadcrumbs, memory snapshot) directly into <gameDir>/logcat/crash_<epoch>_<pid>.log
// so the user-facing feedback bundle captures the crash without needing logcat access.
void crash_breadcrumb(int signal, siginfo_t* info, void* ctx);
// Must be called once at client init (normal context, not signal-safe) so the
// handler can compose the crash file path with zero allocation later.
void crash_set_report_dir(const char* gameDir);
