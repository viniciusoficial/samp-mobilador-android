//
// Created by x1y2z on 20.03.2023.
//

#ifndef LIVERUSSIA_CSTACKTRACE_H
#define LIVERUSSIA_CSTACKTRACE_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <unwind.h>
#include <android/log.h>
#include "main.h"
#include "log.h"

void Log(const char* fmt, ...);

extern uintptr_t g_libSAMP;
extern int g_iLastRenderedObject;

#if defined(__aarch64__)
#define PRINT_CRASH_STATES(context) \
    Log("1: libGame.so + 0x%llx", (unsigned long long)((context)->uc_mcontext.pc - g_libGTASA)); \
    Log("2: libGame.so + 0x%llx", (unsigned long long)((context)->uc_mcontext.regs[30] - g_libGTASA)); \
    Log("1: libsamp.so + 0x%llx", (unsigned long long)((context)->uc_mcontext.pc - g_libSAMP)); \
    Log("2: libsamp.so + 0x%llx", (unsigned long long)((context)->uc_mcontext.regs[30] - g_libSAMP));
#else
#define PRINT_CRASH_STATES(context) \
    Log("register states:"); \
    Log("r0: 0x%X, r1: 0x%X, r2: 0x%X, r3: 0x%X", (context)->uc_mcontext.arm_r0, (context)->uc_mcontext.arm_r1, (context)->uc_mcontext.arm_r2, (context)->uc_mcontext.arm_r3); \
    Log("r4: 0x%x, r5: 0x%x, r6: 0x%x, r7: 0x%x", (context)->uc_mcontext.arm_r4, (context)->uc_mcontext.arm_r5, (context)->uc_mcontext.arm_r6, (context)->uc_mcontext.arm_r7); \
    Log("r8: 0x%x, r9: 0x%x, sl: 0x%x, fp: 0x%x", (context)->uc_mcontext.arm_r8, (context)->uc_mcontext.arm_r9, (context)->uc_mcontext.arm_r10, (context)->uc_mcontext.arm_fp); \
    Log("ip: 0x%x, sp: 0x%x, lr: 0x%x, pc: 0x%x", (context)->uc_mcontext.arm_ip, (context)->uc_mcontext.arm_sp, (context)->uc_mcontext.arm_lr, (context)->uc_mcontext.arm_pc); \
    Log("1: libGame.so + 0x%X", (unsigned int)((context)->uc_mcontext.arm_pc - g_libGTASA)); \
    Log("2: libGame.so + 0x%X", (unsigned int)((context)->uc_mcontext.arm_lr - g_libGTASA)); \
    Log("1: libsamp.so + 0x%X", (unsigned int)((context)->uc_mcontext.arm_pc - g_libSAMP)); \
    Log("2: libsamp.so + 0x%X", (unsigned int)((context)->uc_mcontext.arm_lr - g_libSAMP));
#endif

class CStackTrace
{
public:
    struct CrashTraceState
    {
        int fd;
        int depth;
    };

    static void WriteLine(int fd, const char* fmt, ...)
    {
        char line[1024];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(line, sizeof(line) - 2, fmt, args);
        va_end(args);
        if (len < 0) return;
        if (len >= static_cast<int>(sizeof(line) - 2)) {
            len = sizeof(line) - 2;
        }
        line[len] = '\0';
        __android_log_write(ANDROID_LOG_ERROR, "SAMP-CRASH", line);
        if (fd >= 0) {
            line[len] = '\n';
            line[len + 1] = '\0';
            ssize_t written = 0;
            const ssize_t total = len + 1;
            while (written < total) {
                ssize_t n = write(fd, line + written, static_cast<size_t>(total - written));
                if (n <= 0) break;
                written += n;
            }
        }
    }

    static const char* ShortLibName(const char* path)
    {
        if (!path) return "unknown";
        const char* slash = strrchr(path, '/');
        return slash ? (slash + 1) : path;
    }

    static void LogResolvedAddress(int fd, const char* label, uintptr_t addr)
    {
        Dl_info info{};
        if (addr != 0 && dladdr(reinterpret_cast<void*>(addr), &info) != 0 && info.dli_fname != nullptr) {
            uintptr_t modBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
            uintptr_t modOff = addr - modBase;
            const char* libName = ShortLibName(info.dli_fname);
            if (info.dli_sname != nullptr) {
                uintptr_t symOff = addr - reinterpret_cast<uintptr_t>(info.dli_saddr);
                WriteLine(fd, "%s: %p | %s + 0x%llx | func: %s + 0x%llx",
                          label,
                          reinterpret_cast<void*>(addr),
                          libName,
                          static_cast<unsigned long long>(modOff),
                          info.dli_sname,
                          static_cast<unsigned long long>(symOff));
            } else {
                WriteLine(fd, "%s: %p | %s + 0x%llx | func: (symbol unknown)",
                          label,
                          reinterpret_cast<void*>(addr),
                          libName,
                          static_cast<unsigned long long>(modOff));
            }
        } else {
            WriteLine(fd, "%s: %p | module unknown (samp: +0x%llx, gta: +0x%llx)",
                      label,
                      reinterpret_cast<void*>(addr),
                      static_cast<unsigned long long>(addr >= g_libSAMP ? (addr - g_libSAMP) : 0),
                      static_cast<unsigned long long>(addr >= g_libGTASA ? (addr - g_libGTASA) : 0));
        }
    }

    static void printBacktrace()
    {
        Log("Last rendered object: %d", g_iLastRenderedObject);
        Log("------------ START BACKTRACE ------------");
        Log(" ");
        PrintStackTrace();
    }

    static void PrintCrashReport(int signum, const siginfo_t* info, const ucontext_t* context)
    {
        char logPath[512];
        const char* baseStorage = (g_pszStorage && g_pszStorage[0]) ? g_pszStorage : "/storage/emulated/0/GTA/";
        snprintf(logPath, sizeof(logPath), "%slogcat.txt", baseStorage);

        int fd = open(logPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd < 0) {
            fd = open("/storage/emulated/0/GTA/logcat.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
        }

        const char* sigName = "UNKNOWN";
        switch (signum) {
            case SIGSEGV: sigName = "SIGSEGV"; break;
            case SIGABRT: sigName = "SIGABRT"; break;
            case SIGFPE:  sigName = "SIGFPE";  break;
            case SIGBUS:  sigName = "SIGBUS";  break;
            case SIGILL:  sigName = "SIGILL";  break;
            default: break;
        }

        const char* codeReason = "unknown";
        if (info) {
            if (signum == SIGSEGV) {
                if (info->si_code == SEGV_MAPERR) codeReason = "SEGV_MAPERR (unmapped memory / null pointer)";
                else if (info->si_code == SEGV_ACCERR) codeReason = "SEGV_ACCERR (invalid permissions)";
            } else if (signum == SIGBUS) {
                if (info->si_code == BUS_ADRALN) codeReason = "BUS_ADRALN (invalid alignment)";
                else if (info->si_code == BUS_ADRERR) codeReason = "BUS_ADRERR (nonexistent physical address)";
            } else if (signum == SIGILL) {
                codeReason = "SIGILL (illegal instruction)";
            } else if (signum == SIGFPE) {
                codeReason = "SIGFPE (arithmetic exception)";
            } else if (signum == SIGABRT) {
                codeReason = "SIGABRT (abort / assertion / exception)";
            }
        }

        long tid = syscall(SYS_gettid);
        WriteLine(fd, "================ CRASH REPORT ================");
        WriteLine(fd, "Signal: %s (%d) | Code: %d (%s) | Fault address: %p | TID: %ld",
                  sigName, signum, info ? info->si_code : 0, codeReason,
                  info ? info->si_addr : nullptr, tid);
        WriteLine(fd, "Base addresses: libGame.so=%p | libsamp.so=%p",
                  reinterpret_cast<void*>(g_libGTASA),
                  reinterpret_cast<void*>(g_libSAMP));
        WriteLine(fd, "Context: LastRenderedObj=%d", g_iLastRenderedObject);

        // Ultimos avisos/erros, com arquivo:linha, para saber o que falhou antes do crash.
        const int nRecent = LogRecentCount();
        if (nRecent > 0)
        {
            WriteLine(fd, "------------ LAST ERRORS (file:line) ------------");
            for (int i = 0; i < nRecent; ++i)
            {
                const char* recent = LogRecentAt(i);
                if (recent) WriteLine(fd, "  %s", recent);
            }
            WriteLine(fd, "-------------------------------------------------");
        }

        if (context) {
#if defined(__aarch64__)
            uintptr_t pc = static_cast<uintptr_t>(context->uc_mcontext.pc);
            uintptr_t lr = static_cast<uintptr_t>(context->uc_mcontext.regs[30]);
            uintptr_t sp = static_cast<uintptr_t>(context->uc_mcontext.sp);
            uintptr_t fp = static_cast<uintptr_t>(context->uc_mcontext.regs[29]);
            LogResolvedAddress(fd, "CRASH PC", pc);
            LogResolvedAddress(fd, "CRASH LR", lr);
            WriteLine(fd, "libGame.so PC=+0x%llx LR=+0x%llx | libsamp.so PC=+0x%llx LR=+0x%llx",
                      static_cast<unsigned long long>(pc >= g_libGTASA ? (pc - g_libGTASA) : 0),
                      static_cast<unsigned long long>(lr >= g_libGTASA ? (lr - g_libGTASA) : 0),
                      static_cast<unsigned long long>(pc >= g_libSAMP ? (pc - g_libSAMP) : 0),
                      static_cast<unsigned long long>(lr >= g_libSAMP ? (lr - g_libSAMP) : 0));
            WriteLine(fd, "SP=%p FP=%p PSTATE=0x%llx",
                      reinterpret_cast<void*>(sp), reinterpret_cast<void*>(fp),
                      static_cast<unsigned long long>(context->uc_mcontext.pstate));
            for (int r = 0; r < 30; r += 4) {
                if (r + 3 < 30) {
                    WriteLine(fd, "x%02d=0x%016llx x%02d=0x%016llx x%02d=0x%016llx x%02d=0x%016llx",
                              r, static_cast<unsigned long long>(context->uc_mcontext.regs[r]),
                              r + 1, static_cast<unsigned long long>(context->uc_mcontext.regs[r + 1]),
                              r + 2, static_cast<unsigned long long>(context->uc_mcontext.regs[r + 2]),
                              r + 3, static_cast<unsigned long long>(context->uc_mcontext.regs[r + 3]));
                } else {
                    WriteLine(fd, "x%02d=0x%016llx x%02d=0x%016llx",
                              r, static_cast<unsigned long long>(context->uc_mcontext.regs[r]),
                              r + 1, static_cast<unsigned long long>(context->uc_mcontext.regs[r + 1]));
                }
            }
#else
            uintptr_t pc = static_cast<uintptr_t>(context->uc_mcontext.arm_pc);
            uintptr_t lr = static_cast<uintptr_t>(context->uc_mcontext.arm_lr);
            LogResolvedAddress(fd, "CRASH PC", pc);
            LogResolvedAddress(fd, "CRASH LR", lr);
            WriteLine(fd, "libGame.so PC=+0x%x LR=+0x%x | libsamp.so PC=+0x%x LR=+0x%x",
                      static_cast<unsigned int>(pc >= g_libGTASA ? (pc - g_libGTASA) : 0),
                      static_cast<unsigned int>(lr >= g_libGTASA ? (lr - g_libGTASA) : 0),
                      static_cast<unsigned int>(pc >= g_libSAMP ? (pc - g_libSAMP) : 0),
                      static_cast<unsigned int>(lr >= g_libSAMP ? (lr - g_libSAMP) : 0));
            WriteLine(fd, "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x",
                      context->uc_mcontext.arm_r0, context->uc_mcontext.arm_r1,
                      context->uc_mcontext.arm_r2, context->uc_mcontext.arm_r3);
            WriteLine(fd, "r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x",
                      context->uc_mcontext.arm_r4, context->uc_mcontext.arm_r5,
                      context->uc_mcontext.arm_r6, context->uc_mcontext.arm_r7);
            WriteLine(fd, "r8=0x%08x r9=0x%08x sl=0x%08x fp=0x%08x",
                      context->uc_mcontext.arm_r8, context->uc_mcontext.arm_r9,
                      context->uc_mcontext.arm_r10, context->uc_mcontext.arm_fp);
            WriteLine(fd, "ip=0x%08x sp=0x%08x lr=0x%08x pc=0x%08x",
                      context->uc_mcontext.arm_ip, context->uc_mcontext.arm_sp,
                      context->uc_mcontext.arm_lr, context->uc_mcontext.arm_pc);
#endif
        }

        WriteLine(fd, "------------ START BACKTRACE ------------");
        CrashTraceState state{fd, 0};
        _Unwind_Backtrace(CrashTraceFunction, &state);
        WriteLine(fd, "============= END CRASH REPORT =============");

        if (fd >= 0) {
            fsync(fd);
            close(fd);
        }
    }

private:
    static _Unwind_Reason_Code CrashTraceFunction(_Unwind_Context* context, void* arg)
    {
        CrashTraceState* state = static_cast<CrashTraceState*>(arg);
        if (!state || state->depth >= 48) {
            return _URC_END_OF_STACK;
        }
        uintptr_t pc = _Unwind_GetIP(context);
        if (!pc) {
            return _URC_NO_REASON;
        }
        char label[24];
        snprintf(label, sizeof(label), "  #%02d", state->depth++);
        LogResolvedAddress(state->fd, label, pc);
        return _URC_NO_REASON;
    }

    static _Unwind_Reason_Code TraceFunction(_Unwind_Context* context, void* arg) {
        uintptr_t pc = _Unwind_GetIP(context);
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname != nullptr) {
            uintptr_t modOff = pc - reinterpret_cast<uintptr_t>(info.dli_fbase);
            Log("[adr: %p %s+0x%llx samp: +0x%llx gta: +0x%llx] %s",
                reinterpret_cast<void*>(pc),
                ShortLibName(info.dli_fname),
                static_cast<unsigned long long>(modOff),
                static_cast<unsigned long long>(pc >= g_libSAMP ? (pc - g_libSAMP) : 0),
                static_cast<unsigned long long>(pc >= g_libGTASA ? (pc - g_libGTASA) : 0),
                info.dli_sname ? info.dli_sname : "(symbol unknown)");
        } else {
            Log("[adr: %p samp: +0x%llx gta: +0x%llx] name not found",
                reinterpret_cast<void*>(pc),
                static_cast<unsigned long long>(pc >= g_libSAMP ? (pc - g_libSAMP) : 0),
                static_cast<unsigned long long>(pc >= g_libGTASA ? (pc - g_libGTASA) : 0));
        }
        return _URC_NO_REASON;
    }

    static void PrintStackTrace() {
        _Unwind_Backtrace(TraceFunction, nullptr);
    }
};

#endif //LIVERUSSIA_CSTACKTRACE_H
