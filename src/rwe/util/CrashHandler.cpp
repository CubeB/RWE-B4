#include <rwe/util/CrashHandler.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <rwe/config.h>
#include <rwe/util/SimpleLogger.h>

#ifdef RWE_PLATFORM_WINDOWS
#include <windows.h>
// dbghelp.h must follow windows.h.
#include <dbghelp.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <csignal>
#include <cstdlib>
#include <exception>
#else
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#endif

#include <SDL3/SDL.h>

namespace rwe
{
    namespace
    {
        /**
         * Bounded, non-allocating text appender. Everything the handler writes
         * goes through this: it truncates at the end of the buffer rather than
         * growing it, and keeps the buffer terminated at every step.
         */
        struct Appender
        {
            char* buf;
            size_t size;
            size_t pos{0};

            Appender(char* buf, size_t size) : buf(buf), size(size)
            {
                if (size > 0)
                {
                    buf[0] = '\0';
                }
            }

            void ch(char c)
            {
                if (size == 0 || pos + 1 >= size)
                {
                    return;
                }
                buf[pos++] = c;
                buf[pos] = '\0';
            }

            void str(const char* s)
            {
                if (s == nullptr)
                {
                    s = "(null)";
                }
                for (; *s != '\0'; ++s)
                {
                    ch(*s);
                }
            }

            void uint(uint64_t value)
            {
                // 20 digits covers UINT64_MAX; built backwards then flushed.
                char tmp[21];
                int i = 0;
                if (value == 0)
                {
                    tmp[i++] = '0';
                }
                while (value > 0)
                {
                    tmp[i++] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                }
                while (i > 0)
                {
                    ch(tmp[--i]);
                }
            }

            void hex(uint64_t value, int digits)
            {
                str("0x");
                for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
                {
                    ch("0123456789abcdef"[(value >> shift) & 0xF]);
                }
            }

            /** A "label:<pad>" column so the report reads as a table. */
            void field(const char* label)
            {
                str(label);
                str(":");
                for (size_t i = std::strlen(label); i < 12; ++i)
                {
                    ch(' ');
                }
            }
        };

        void copyName(char* dest, size_t destSize, const char* src)
        {
            if (destSize == 0)
            {
                return;
            }
            if (src == nullptr)
            {
                src = "none";
            }
            size_t i = 0;
            for (; i + 1 < destSize && src[i] != '\0'; ++i)
            {
                dest[i] = src[i];
            }
            dest[i] = '\0';
        }

        std::atomic_flag entered = ATOMIC_FLAG_INIT;

        // Formatted once at install time: the handler must not touch
        // std::filesystem, and by the time it runs there may be no heap left
        // to build a path with anyway.
        char crashPath[1024] = {0};

        // The report is assembled here rather than on the stack, because the
        // fault we most want to catch is a stack overflow.
        char reportBuffer[16384];

        bool dialogSuppressed()
        {
            // Set by crash_probe, and by anything else that must not block on
            // a message box nobody is there to dismiss.
            return std::getenv("RWE_CRASH_NO_DIALOG") != nullptr;
        }
    }

    const char* crashPhaseName(CrashPhase p)
    {
        switch (p)
        {
            case CrashPhase::Startup: return "Startup";
            case CrashPhase::Input: return "Input";
            case CrashPhase::Update: return "Update";
            case CrashPhase::SimTick: return "SimTick";
            case CrashPhase::Render: return "Render";
            case CrashPhase::Swap: return "Swap";
            case CrashPhase::Shutdown: return "Shutdown";
        }
        return "Unknown";
    }

    CrashContext& crashContext()
    {
        static CrashContext ctx;
        return ctx;
    }

    void setCrashPhase(CrashPhase p)
    {
        crashContext().phase.store(static_cast<uint32_t>(p), std::memory_order_relaxed);
    }

    void setCrashScene(const char* name)
    {
        auto& ctx = crashContext();
        copyName(ctx.scene, CrashContext::NameSize, name);
    }

    void setCrashMap(const char* name)
    {
        auto& ctx = crashContext();
        copyName(ctx.map, CrashContext::NameSize, name);
    }

    void setCrashNote(const char* text)
    {
        auto& ctx = crashContext();
        copyName(ctx.note, CrashContext::NoteSize, text);
    }

    void setCrashAssertion(const char* assertion, const char* file, unsigned int line, const char* function)
    {
        auto& ctx = crashContext();
        Appender a(ctx.note, CrashContext::NoteSize);
        a.str(file);
        a.ch(':');
        a.uint(line);
        a.str(": ");
        a.str(function);
        a.str(": Assertion `");
        a.str(assertion);
        a.str("' failed.");
    }

    void setCrashTick(uint32_t sceneTime, uint32_t gameTime, uint32_t unitCount, uint32_t playerCount)
    {
        auto& ctx = crashContext();
        ctx.sceneTime.store(sceneTime, std::memory_order_relaxed);
        ctx.gameTime.store(gameTime, std::memory_order_relaxed);
        ctx.unitCount.store(unitCount, std::memory_order_relaxed);
        ctx.playerCount.store(playerCount, std::memory_order_relaxed);
    }

    size_t formatCrashReport(
        char* buf,
        size_t bufSize,
        const char* faultName,
        const void* faultAddress,
        const CrashContext& ctx)
    {
        Appender a(buf, bufSize);

        a.str("RWE crash report\n");
        a.str("================\n\n");

        a.field("version");
        a.str(ProjectNameVersion.c_str());
        a.str(" (");
        a.str(RevivalTitle.c_str());
        a.str(")\n");

        a.field("fault");
        a.str(faultName);
        a.ch('\n');

        a.field("address");
        a.hex(reinterpret_cast<uint64_t>(faultAddress), static_cast<int>(sizeof(void*)) * 2);
        a.ch('\n');

        a.field("phase");
        a.str(crashPhaseName(static_cast<CrashPhase>(ctx.phase.load(std::memory_order_relaxed))));
        a.ch('\n');

        a.field("scene");
        a.str(ctx.scene);
        a.ch('\n');

        a.field("map");
        a.str(ctx.map);
        a.ch('\n');

        a.field("scene tick");
        a.uint(ctx.sceneTime.load(std::memory_order_relaxed));
        a.ch('\n');

        a.field("game time");
        a.uint(ctx.gameTime.load(std::memory_order_relaxed));
        a.ch('\n');

        a.field("unit slots");
        a.uint(ctx.unitCount.load(std::memory_order_relaxed));
        a.ch('\n');

        a.field("players");
        a.uint(ctx.playerCount.load(std::memory_order_relaxed));
        a.ch('\n');

        if (ctx.note[0] != '\0')
        {
            a.field("note");
            a.str(ctx.note);
            a.ch('\n');
        }

        a.ch('\n');
        a.str("backtrace:\n");
        // The handler is itself on the stack it is printing, so the top few
        // frames are always this file. They are left in rather than skipped by
        // a hardcoded count, which inlining would make wrong.
        a.str("  (the first few frames are the crash handler; the fault is below them)\n");

        return a.pos;
    }

    bool formatCrashFilePath(char* buf, size_t bufSize, const char* dir, std::time_t when)
    {
        std::tm tm{};
#ifdef RWE_PLATFORM_WINDOWS
        localtime_s(&tm, &when);
#else
        localtime_r(&when, &tm);
#endif

        Appender a(buf, bufSize);
        a.str(dir);
        if (dir != nullptr && dir[0] != '\0')
        {
            auto last = dir[std::strlen(dir) - 1];
            if (last != '/' && last != '\\')
            {
                a.ch('/');
            }
        }
        a.str("rwe-crash-");

        auto two = [&a](int value) {
            a.ch(static_cast<char>('0' + ((value / 10) % 10)));
            a.ch(static_cast<char>('0' + (value % 10)));
        };

        a.uint(static_cast<uint64_t>(tm.tm_year + 1900));
        two(tm.tm_mon + 1);
        two(tm.tm_mday);
        a.ch('-');
        two(tm.tm_hour);
        two(tm.tm_min);
        two(tm.tm_sec);
        a.str(".txt");

        // Truncation means the name is not the one we asked for, and a crash
        // file under a wrong name is worse than an honest failure to install.
        return a.pos + 1 < bufSize;
    }

    bool claimCrashHandlerEntry()
    {
        return !entered.test_and_set(std::memory_order_acq_rel);
    }

    void resetCrashHandlerEntryForTesting()
    {
        entered.clear(std::memory_order_release);
    }

    const char* crashFilePath()
    {
        return crashPath;
    }


    namespace
    {
        /**
         * Leave immediately, without unwinding, running atexit handlers or
         * flushing anything. The process state is not trustworthy by this
         * point and the report is already on disk.
         */
        [[noreturn]] void fastExit()
        {
#ifdef RWE_PLATFORM_WINDOWS
            TerminateProcess(GetCurrentProcess(), 3);
            // TerminateProcess does not return for the current process, but
            // the compiler does not know that.
            for (;;)
            {
            }
#else
            _exit(3);
#endif
        }

#ifdef RWE_PLATFORM_WINDOWS
        using CrashFile = HANDLE;
        // const rather than constexpr: INVALID_HANDLE_VALUE is a cast from -1
        // to a pointer, which is not a constant expression.
        const CrashFile InvalidCrashFile = INVALID_HANDLE_VALUE;

        CrashFile openCrashFile()
        {
            return CreateFileA(
                crashPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }

        void writeAll(CrashFile file, const char* data, size_t length)
        {
            while (length > 0)
            {
                DWORD written = 0;
                if (!WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr) || written == 0)
                {
                    return;
                }
                data += written;
                length -= written;
            }
        }

        void closeCrashFile(CrashFile file)
        {
            CloseHandle(file);
        }

        // Static, because a stack overflow is exactly the fault this is here
        // to report and there is no room left on the stack to build a line.
        char frameBuffer[2048];
        void* frames[62];

        void appendBacktrace(CrashFile file)
        {
            auto process = GetCurrentProcess();
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            auto haveSymbols = SymInitialize(process, nullptr, TRUE) != FALSE;

            auto count = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
            auto namedAny = false;

            alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + 512];
            for (WORD i = 0; i < count; ++i)
            {
                auto address = reinterpret_cast<DWORD64>(frames[i]);

                Appender a(frameBuffer, sizeof(frameBuffer));
                a.str("  #");
                a.uint(i);
                a.str("  ");
                a.hex(address, static_cast<int>(sizeof(void*)) * 2);
                a.str("  ");

                // The module and offset are always available and are what
                // addr2line needs, so they go in whether or not dbghelp can
                // name the function.
                HMODULE module = nullptr;
                if (GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCSTR>(frames[i]),
                        &module)
                    && module != nullptr)
                {
                    char moduleName[MAX_PATH];
                    if (GetModuleFileNameA(module, moduleName, MAX_PATH) > 0)
                    {
                        auto base = moduleName;
                        for (auto p = moduleName; *p != '\0'; ++p)
                        {
                            if (*p == '\\' || *p == '/')
                            {
                                base = p + 1;
                            }
                        }
                        a.str(base);
                        a.ch('+');
                        a.hex(address - reinterpret_cast<DWORD64>(module), 8);
                    }
                }

                if (haveSymbols)
                {
                    auto symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
                    std::memset(symbolStorage, 0, sizeof(symbolStorage));
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen = 511;

                    DWORD64 symbolOffset = 0;
                    if (SymFromAddr(process, address, &symbolOffset, symbol))
                    {
                        namedAny = true;
                        a.str("  ");
                        a.str(symbol->Name);

                        IMAGEHLP_LINE64 line;
                        std::memset(&line, 0, sizeof(line));
                        line.SizeOfStruct = sizeof(line);
                        DWORD lineOffset = 0;
                        if (SymGetLineFromAddr64(process, address, &lineOffset, &line))
                        {
                            a.str("  (");
                            a.str(line.FileName);
                            a.ch(':');
                            a.uint(line.LineNumber);
                            a.ch(')');
                        }
                    }
                }

                a.ch('\n');
                writeAll(file, frameBuffer, a.pos);
            }

            if (!namedAny)
            {
                // MinGW emits DWARF and dbghelp reads PDBs, so a MinGW build
                // gets addresses and nothing else. Say so here rather than let
                // it look like a broken backtrace, and say what resolves it.
                Appender a(frameBuffer, sizeof(frameBuffer));
                a.str("\nNo symbol names available (a MinGW build stores DWARF, which dbghelp\n");
                a.str("cannot read). Resolve a frame with the module+offset above:\n");
                a.str("    addr2line -f -C -e <module> <offset>\n");
                writeAll(file, frameBuffer, a.pos);
            }
        }
#else
        using CrashFile = int;
        const CrashFile InvalidCrashFile = -1;

        CrashFile openCrashFile()
        {
            return ::open(crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }

        void writeAll(CrashFile file, const char* data, size_t length)
        {
            while (length > 0)
            {
                auto written = ::write(file, data, length);
                if (written <= 0)
                {
                    return;
                }
                data += written;
                length -= static_cast<size_t>(written);
            }
        }

        void closeCrashFile(CrashFile file)
        {
            ::close(file);
        }

        void* frames[64];

        void appendBacktrace(CrashFile file)
        {
#ifdef __GLIBC__
            auto count = backtrace(frames, static_cast<int>(std::size(frames)));
            // Writes straight to the descriptor, which is the one form of this
            // that does not allocate -- backtrace_symbols() does, and calling
            // it here would deadlock on the allocator we may have crashed in.
            backtrace_symbols_fd(frames, count, file);
#else
            static const char message[] = "  (no backtrace: not a glibc build)\n";
            writeAll(file, message, sizeof(message) - 1);
#endif
        }
#endif

        /**
         * The whole of what happens on a fault: claim the entry, write the
         * report, tell the user, leave. Nothing here allocates or locks.
         */
        [[noreturn]] void reportAndDie(const char* faultName, const void* faultAddress)
        {
            if (!claimCrashHandlerEntry())
            {
                // A second fault, raised from inside this handler. Trying
                // again would only loop.
                fastExit();
            }

            if (crashPath[0] != '\0')
            {
                auto length = formatCrashReport(
                    reportBuffer, sizeof(reportBuffer), faultName, faultAddress, crashContext());

                auto file = openCrashFile();
                if (file != InvalidCrashFile)
                {
                    writeAll(file, reportBuffer, length);
                    appendBacktrace(file);
                    closeCrashFile(file);
                }
            }

            if (!dialogSuppressed())
            {
                // The report is already on disk, so this is only about telling
                // the user where to look; it is safe for it to fail.
                Appender a(reportBuffer, sizeof(reportBuffer));
                a.str("RWE has crashed (");
                a.str(faultName);
                a.str(").\n\nA report was written to:\n");
                a.str(crashPath[0] != '\0' ? crashPath : "(nowhere: the handler was not installed)");
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "RWE crashed", reportBuffer, nullptr);
            }

            fastExit();
        }

        [[noreturn]] void terminateHandler()
        {
            reportAndDie("std::terminate (uncaught exception)", nullptr);
        }

#ifdef RWE_PLATFORM_WINDOWS
        // Windows only: the POSIX side takes SIGABRT through sigaction with
        // the rest of the fatal signals.
        void abortHandler(int)
        {
            reportAndDie("SIGABRT (abort)", nullptr);
        }

#if defined(_MSC_VER) && defined(_DEBUG)
        // MSVC has no __assert_fail to replace, but the debug CRT offers a
        // report hook, which is the supported way in. Returning FALSE leaves
        // the CRT to carry on and abort as it would have.
        int crtReportHook(int reportType, char* message, int* /*returnValue*/)
        {
            if (reportType == _CRT_ASSERT && message != nullptr)
            {
                setCrashNote(message);
            }
            return FALSE;
        }
#endif

        LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* info)
        {
            const char* name = "unknown exception";
            const void* address = nullptr;

            if (info != nullptr && info->ExceptionRecord != nullptr)
            {
                address = info->ExceptionRecord->ExceptionAddress;
                switch (info->ExceptionRecord->ExceptionCode)
                {
                    case EXCEPTION_ACCESS_VIOLATION:
                        name = "EXCEPTION_ACCESS_VIOLATION";
                        // For an access violation the interesting address is
                        // the one it tried to touch, not the instruction.
                        if (info->ExceptionRecord->NumberParameters >= 2)
                        {
                            address = reinterpret_cast<const void*>(
                                info->ExceptionRecord->ExceptionInformation[1]);
                        }
                        break;
                    case EXCEPTION_STACK_OVERFLOW: name = "EXCEPTION_STACK_OVERFLOW"; break;
                    case EXCEPTION_ILLEGAL_INSTRUCTION: name = "EXCEPTION_ILLEGAL_INSTRUCTION"; break;
                    case EXCEPTION_INT_DIVIDE_BY_ZERO: name = "EXCEPTION_INT_DIVIDE_BY_ZERO"; break;
                    case EXCEPTION_FLT_DIVIDE_BY_ZERO: name = "EXCEPTION_FLT_DIVIDE_BY_ZERO"; break;
                    case EXCEPTION_PRIV_INSTRUCTION: name = "EXCEPTION_PRIV_INSTRUCTION"; break;
                    case EXCEPTION_IN_PAGE_ERROR: name = "EXCEPTION_IN_PAGE_ERROR"; break;
                    default: break;
                }
            }

            reportAndDie(name, address);
        }
#else
        // A stack overflow cannot be handled on the stack that overflowed, and
        // that is one of the faults most worth catching, so the handler runs
        // on a stack of its own.
        char alternateStack[64 * 1024];

        void signalHandler(int sig, siginfo_t* info, void*)
        {
            const char* name = "unknown signal";
            switch (sig)
            {
                case SIGSEGV: name = "SIGSEGV (segmentation fault)"; break;
                case SIGBUS: name = "SIGBUS (bus error)"; break;
                case SIGFPE: name = "SIGFPE (arithmetic error)"; break;
                case SIGILL: name = "SIGILL (illegal instruction)"; break;
                case SIGABRT: name = "SIGABRT (abort)"; break;
                default: break;
            }

            reportAndDie(name, info == nullptr ? nullptr : info->si_addr);
        }
#endif
    }

    void installCrashHandler(const std::filesystem::path& localDataPath)
    {
        if (!formatCrashFilePath(crashPath, sizeof(crashPath), localDataPath.string().c_str(), std::time(nullptr)))
        {
            crashPath[0] = '\0';
            LOG_WARN << "Crash handler not installed: local data path too long";
            return;
        }

        std::set_terminate(terminateHandler);

#ifdef RWE_PLATFORM_WINDOWS
        SetUnhandledExceptionFilter(exceptionFilter);
#if defined(_MSC_VER) && defined(_DEBUG)
        _CrtSetReportHook(crtReportHook);
#endif
        // abort() does not go through the unhandled exception filter, and a
        // failed assert() is an abort.
        std::signal(SIGABRT, abortHandler);
#else
        stack_t altStack{};
        altStack.ss_sp = alternateStack;
        altStack.ss_size = sizeof(alternateStack);
        altStack.ss_flags = 0;
        sigaltstack(&altStack, nullptr);

        struct sigaction action
        {
        };
        action.sa_sigaction = signalHandler;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
        sigemptyset(&action.sa_mask);

        for (auto sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        {
            sigaction(sig, &action, nullptr);
        }
#endif

        LOG_INFO << "Crash handler installed; a fault would be reported to " << crashPath;
    }
}

// Replacements for the C library's assertion failure routine. The library
// writes its message to stderr and then aborts; nothing captured that text, so
// a failed assertion produced a report that said SIGABRT and left you to work
// out which assertion from the backtrace alone. These record the message first
// and then do exactly what the library would have: print the same line to
// stderr, and abort, so the console output and the signal are unchanged.
//
// Defining these here overrides the C library's own, because the linker
// resolves from our objects before it reaches libc.

#if defined(__GLIBC__)
extern "C" [[noreturn]] void __assert_fail(
    const char* assertion, const char* file, unsigned int line, const char* function) noexcept
{
    rwe::setCrashAssertion(assertion, file, line, function);
    std::fprintf(stderr, "%s:%u: %s: Assertion `%s' failed.\n", file, line, function, assertion);
    std::fflush(stderr);
    std::abort();
}
#elif defined(__MINGW32__)
extern "C" [[noreturn]] void _assert(const char* message, const char* file, unsigned line)
{
    rwe::setCrashAssertion(message, file, line, "");
    std::fprintf(stderr, "Assertion failed: %s, file %s, line %u\n", message, file, line);
    std::fflush(stderr);
    std::abort();
}
#endif
