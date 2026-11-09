#include "crash_log.hpp"
#include "storage.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::filesystem::path g_sessionMarker;

constexpr size_t BREADCRUMB_SLOTS = 32;
constexpr size_t BREADCRUMB_SIZE = 160;
char g_breadcrumbs[BREADCRUMB_SLOTS][BREADCRUMB_SIZE] = {};
std::atomic<unsigned> g_breadcrumbCount{0};
char g_header[512] = {};
const auto g_sessionStart = std::chrono::steady_clock::now();

void recordBreadcrumb(std::string_view message) {
    const unsigned sequence = g_breadcrumbCount.load(std::memory_order_relaxed);
    char* slot = g_breadcrumbs[sequence % BREADCRUMB_SLOTS];
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - g_sessionStart).count();
    const int prefix = std::snprintf(slot, BREADCRUMB_SIZE, "[%9.2fs] ", seconds);
    const size_t start = prefix > 0 ? static_cast<size_t>(prefix) : 0;
    const size_t size = std::min(message.size(), BREADCRUMB_SIZE - 1 - start);
    std::memcpy(slot + start, message.data(), size);
    slot[start + size] = '\0';
    g_breadcrumbCount.store(sequence + 1, std::memory_order_release);
}

void captureHeader() {
    std::string text;
    try {
        text = fmt::format("Grape {} | Geometry Dash {} | Geode {}",
                           geode::Mod::get()->getVersion().toVString(),
                           geode::Loader::get()->getGameVersion(),
                           geode::Loader::get()->getVersion().toVString());
    } catch (...) {
        text = "Grape (version unavailable)";
    }
    std::snprintf(g_header, sizeof(g_header), "%s", text.c_str());
}

template <class Write>
void forEachBreadcrumb(Write&& write) {
    const unsigned total = g_breadcrumbCount.load(std::memory_order_acquire);
    const unsigned shown = std::min<unsigned>(total, BREADCRUMB_SLOTS);
    for (unsigned i = total - shown; i < total; ++i)
        write(g_breadcrumbs[i % BREADCRUMB_SLOTS]);
}

struct SessionGuard {
    bool active = false;
    ~SessionGuard() {
        if (!active || g_sessionMarker.empty()) return;
        std::error_code error;
        std::filesystem::remove(g_sessionMarker, error);
    }
};

void installSessionMarker() {
    static SessionGuard guard;
    if (guard.active) return;
#ifdef GEODE_IS_IOS
    const auto directory =
        grape::paths::toStd(geode::dirs::getSaveDir()).parent_path() / "Grape" / "logs";
    std::filesystem::create_directories(directory);
#else
    const auto directory = grape::paths::directory("logs");
#endif
    g_sessionMarker = directory / "session-active.marker";
    if (std::filesystem::exists(g_sessionMarker)) {
        std::ifstream previous(g_sessionMarker);
        std::ofstream log(directory / "forced-exit.log",
                          std::ios::out | std::ios::app);
        log << "\nPrevious Grape session ended without clean shutdown.\n"
            << "Detected at Unix time: " << std::time(nullptr) << "\n"
            << "This includes force-close, SIGKILL, OS termination, or a "
               "crash before the handler ran.\n"
            << "Previous session marker: " << previous.rdbuf() << "\n";
        log.flush();
    }
    std::ofstream marker(g_sessionMarker,
                         std::ios::out | std::ios::trunc);
    marker << "Started at Unix time: " << std::time(nullptr) << "\n";
    marker.flush();
    guard.active = true;
}
}

#ifdef GEODE_IS_WINDOWS

#include <Windows.h>
#include <DbgHelp.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

using SymInitialize_t           = BOOL  (WINAPI*)(HANDLE, PCSTR, BOOL);
using SymCleanup_t              = BOOL  (WINAPI*)(HANDLE);
using SymSetOptions_t           = DWORD (WINAPI*)(DWORD);
using StackWalk64_t             = BOOL  (WINAPI*)(DWORD, HANDLE, HANDLE,
                                                   LPSTACKFRAME64, PVOID,
                                                   PREAD_PROCESS_MEMORY_ROUTINE64,
                                                   PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                                   PGET_MODULE_BASE_ROUTINE64,
                                                   PTRANSLATE_ADDRESS_ROUTINE64);
using SymFromAddr_t             = BOOL  (WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
using SymGetLineFromAddr64_t    = BOOL  (WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
using SymFunctionTableAccess64_t= PVOID (WINAPI*)(HANDLE, DWORD64);
using SymGetModuleBase64_t      = DWORD64(WINAPI*)(HANDLE, DWORD64);

namespace {

template <class Function>
Function loadProcedure(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    static_assert(sizeof(Function) == sizeof(address));
    Function function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

std::filesystem::path makeLogPath() {
    auto dir = grape::paths::directory("logs");

    std::time_t t  = std::time(nullptr);
    std::tm     tm = {};
    localtime_s(&tm, &t);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "crash_%04d%02d%02d_%02d%02d%02d.log",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return dir / buf;
}

constexpr size_t KEPT_CRASH_LOGS = 15;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

void pruneOldCrashLogs() {
    std::error_code error;
    std::vector<std::filesystem::path> logs;
    for (const auto& entry :
         std::filesystem::directory_iterator(grape::paths::directory("logs"), error)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("crash_") && name.ends_with(".log"))
            logs.push_back(entry.path());
    }
    if (logs.size() <= KEPT_CRASH_LOGS) return;
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i + KEPT_CRASH_LOGS < logs.size(); ++i)
        std::filesystem::remove(logs[i], error);
}

void writeLocation(std::ofstream& out, uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory = {};
    char path[MAX_PATH] = "<unknown module>";
    uintptr_t base = 0;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &memory, sizeof(memory)) &&
        memory.AllocationBase) {
        base = reinterpret_cast<uintptr_t>(memory.AllocationBase);
        GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), path, MAX_PATH);
    }
    const char* name = std::strrchr(path, '\\');
    out << (name ? name + 1 : path);
    if (base) out << "+0x" << std::hex << address - base << std::dec;
    out << "  (0x" << std::hex << address << std::dec << ")";
}

static const char* exceptionName(DWORD code) {
    switch (code) {
        case 0xE06D7363: return "C++ exception";
#define CASE(x) case x: return #x
        CASE(EXCEPTION_ACCESS_VIOLATION);
        CASE(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
        CASE(EXCEPTION_BREAKPOINT);
        CASE(EXCEPTION_DATATYPE_MISALIGNMENT);
        CASE(EXCEPTION_FLT_DENORMAL_OPERAND);
        CASE(EXCEPTION_FLT_DIVIDE_BY_ZERO);
        CASE(EXCEPTION_FLT_INEXACT_RESULT);
        CASE(EXCEPTION_FLT_INVALID_OPERATION);
        CASE(EXCEPTION_FLT_OVERFLOW);
        CASE(EXCEPTION_FLT_STACK_CHECK);
        CASE(EXCEPTION_FLT_UNDERFLOW);
        CASE(EXCEPTION_ILLEGAL_INSTRUCTION);
        CASE(EXCEPTION_IN_PAGE_ERROR);
        CASE(EXCEPTION_INT_DIVIDE_BY_ZERO);
        CASE(EXCEPTION_INT_OVERFLOW);
        CASE(EXCEPTION_INVALID_DISPOSITION);
        CASE(EXCEPTION_NONCONTINUABLE_EXCEPTION);
        CASE(EXCEPTION_PRIV_INSTRUCTION);
        CASE(EXCEPTION_SINGLE_STEP);
        CASE(EXCEPTION_STACK_OVERFLOW);
#undef CASE
        default: return "UNKNOWN_EXCEPTION";
    }
}

void writeStackTrace(std::ofstream& out, CONTEXT* ctx) {
    HMODULE hDbgHelp = LoadLibraryA("DbgHelp.dll");
    if (!hDbgHelp) {
        out << "  [DbgHelp.dll could not be loaded]\n";
        return;
    }

    const auto SymInitialize_ =
        loadProcedure<SymInitialize_t>(hDbgHelp, "SymInitialize");
    const auto SymCleanup_ =
        loadProcedure<SymCleanup_t>(hDbgHelp, "SymCleanup");
    const auto SymSetOptions_ =
        loadProcedure<SymSetOptions_t>(hDbgHelp, "SymSetOptions");
    const auto StackWalk64_ =
        loadProcedure<StackWalk64_t>(hDbgHelp, "StackWalk64");
    const auto SymFunctionTableAccess64_ =
        loadProcedure<SymFunctionTableAccess64_t>(
            hDbgHelp, "SymFunctionTableAccess64");
    const auto SymGetModuleBase64_ =
        loadProcedure<SymGetModuleBase64_t>(hDbgHelp, "SymGetModuleBase64");
    const auto SymFromAddr_ =
        loadProcedure<SymFromAddr_t>(hDbgHelp, "SymFromAddr");
    const auto SymGetLineFromAddr64_ =
        loadProcedure<SymGetLineFromAddr64_t>(
            hDbgHelp, "SymGetLineFromAddr64");

    if (!SymInitialize_ || !StackWalk64_ || !SymFromAddr_) {
        out << "  [DbgHelp symbols not available]\n";
        FreeLibrary(hDbgHelp);
        return;
    }

    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread  = GetCurrentThread();

    if (SymSetOptions_)
        SymSetOptions_(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    SymInitialize_(hProcess, nullptr, TRUE);

    STACKFRAME64 frame = {};
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

#ifdef _WIN64
    constexpr DWORD machineType     = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx->Rip;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrStack.Offset = ctx->Rsp;
#else
    constexpr DWORD machineType     = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx->Eip;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrStack.Offset = ctx->Esp;
#endif

    constexpr ULONG kSymBufSize = sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR);
    char symBuf[kSymBufSize]    = {};
    auto* sym                   = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct            = sizeof(SYMBOL_INFO);
    sym->MaxNameLen              = MAX_SYM_NAME;

    int frameIndex = 0;
    while (StackWalk64_(machineType, hProcess, hThread, &frame, ctx,
                        nullptr,
                        SymFunctionTableAccess64_,
                        SymGetModuleBase64_,
                        nullptr)) {
        if (frame.AddrPC.Offset == 0) break;

        out << "  #" << std::setw(2) << frameIndex++ << "  ";
        writeLocation(out, static_cast<uintptr_t>(frame.AddrPC.Offset));

        DWORD64 displacement = 0;
        if (SymFromAddr_(hProcess, frame.AddrPC.Offset, &displacement, sym)) {
            out << "  " << sym->Name << " + 0x" << std::hex << displacement << std::dec;
        }

        if (SymGetLineFromAddr64_) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct    = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisp       = 0;
            if (SymGetLineFromAddr64_(hProcess, frame.AddrPC.Offset, &lineDisp, &line)) {
                out << "  [" << line.FileName << ":" << line.LineNumber << "]";
            }
        }
        out << "\n";

        if (frameIndex > 64) { out << "  ... (truncated)\n"; break; }
    }

    if (SymCleanup_) SymCleanup_(hProcess);
    FreeLibrary(hDbgHelp);
}

void writeRegisters(std::ofstream& out, CONTEXT* ctx) {
#ifdef _WIN64
    out << std::hex;
    out << "  RAX=" << ctx->Rax << "  RBX=" << ctx->Rbx
        << "  RCX=" << ctx->Rcx << "  RDX=" << ctx->Rdx << "\n"
        << "  RSI=" << ctx->Rsi << "  RDI=" << ctx->Rdi
        << "  RSP=" << ctx->Rsp << "  RBP=" << ctx->Rbp << "\n"
        << "  R8 =" << ctx->R8  << "  R9 =" << ctx->R9
        << "  R10=" << ctx->R10 << "  R11=" << ctx->R11 << "\n"
        << "  R12=" << ctx->R12 << "  R13=" << ctx->R13
        << "  R14=" << ctx->R14 << "  R15=" << ctx->R15 << "\n"
        << "  RIP=" << ctx->Rip << "\n";
    out << std::dec;
#else
    out << std::hex;
    out << "  EAX=" << ctx->Eax << "  EBX=" << ctx->Ebx
        << "  ECX=" << ctx->Ecx << "  EDX=" << ctx->Edx << "\n"
        << "  ESI=" << ctx->Esi << "  EDI=" << ctx->Edi
        << "  ESP=" << ctx->Esp << "  EBP=" << ctx->Ebp << "\n"
        << "  EIP=" << ctx->Eip << "\n";
    out << std::dec;
#endif
}

void writeCrashLog(EXCEPTION_POINTERS* ep, const char* reason = nullptr,
                   const char* message = nullptr) {
    try {
        auto path = makeLogPath();
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) return;

        std::time_t t  = std::time(nullptr);
        std::tm     tm = {};
        localtime_s(&tm, &t);
        char tsBuf[64];
        std::snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec);

        out << "========================================\n"
            << "  Grape Crash Log\n"
            << "  Time: " << tsBuf << "\n"
            << "  " << g_header << "\n"
            << "========================================\n\n";

        if (reason) {
            out << "Reason: " << reason << "\n";
        }
        if (message && *message) {
            out << "Message: " << message << "\n";
        }
        out << "Thread:  " << GetCurrentThreadId() << "\n\n";

        if (ep && ep->ExceptionRecord) {
            auto* er = ep->ExceptionRecord;
            out << "Exception: " << exceptionName(er->ExceptionCode)
                << " (0x" << std::hex << er->ExceptionCode << std::dec << ")\n"
                << "Address:   ";
            writeLocation(out, reinterpret_cast<uintptr_t>(er->ExceptionAddress));
            out << "\n";

            if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
                out << "Access:    " << (er->ExceptionInformation[0] == 0 ? "READ" : "WRITE")
                    << " at 0x" << std::hex << er->ExceptionInformation[1] << std::dec << "\n";
            }
            out << "\n";
        }

        if (ep && ep->ContextRecord) {
            out << "Registers:\n";
            writeRegisters(out, ep->ContextRecord);
            out << "\nStack Trace:\n";
            writeStackTrace(out, ep->ContextRecord);
        } else {
            out << "Stack Trace: (no context available)\n";

            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            RtlCaptureContext(&ctx);
            writeStackTrace(out, &ctx);
        }

        out << "\nRecent activity (oldest first):\n";
        bool any = false;
        forEachBreadcrumb([&](const char* line) {
            out << "  " << line << "\n";
            any = true;
        });
        if (!any) out << "  (none recorded)\n";

        out << "\n========================================\n"
            << "  End of crash log\n"
            << "========================================\n";
        out.flush();

        if (g_previousFilter && ep) return;
        const auto text = "Grape has crashed.\n\nThe crash log was saved to:\n" +
                          path.string() + "\n\nOpen the logs folder?";
        if (MessageBoxA(nullptr, text.c_str(), "Grape crash",
                        MB_YESNO | MB_ICONERROR) == IDYES)
            ShellExecuteA(nullptr, "open", path.parent_path().string().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);

    } catch (...) {
    }
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    static std::atomic<bool> handling{false};
    if (!handling.exchange(true))
        writeCrashLog(ep, "Unhandled exception");
    return g_previousFilter ? g_previousFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

void terminateHandler() {
    std::string message = "no active exception";
    if (const auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            message = error.what();
        } catch (...) {
            message = "an exception that is not a std::exception";
        }
    }
    writeCrashLog(nullptr, "std::terminate() called", message.c_str());
    std::abort();
}

}

void crash_log::install() {
    captureHeader();
    g_previousFilter = SetUnhandledExceptionFilter(unhandledExceptionFilter);
    std::set_terminate(terminateHandler);

    grape::paths::directory("logs");
    pruneOldCrashLogs();
    installSessionMarker();
    breadcrumb("Session started");
}

void crash_log::breadcrumb(std::string_view message) {
    recordBreadcrumb(message);
    geode::log::info("[crash breadcrumb] {}", message);
}

#else

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#include <unistd.h>

namespace {
char g_mobileCrashPath[PATH_MAX] = {};

volatile sig_atomic_t g_handlingSignal = 0;
struct sigaction g_previousHandlers[64] = {};

void writeAll(int fd, char const* text, size_t size) {
    while (size > 0) {
        const auto written = ::write(fd, text, size);
        if (written <= 0) return;
        text += written;
        size -= static_cast<size_t>(written);
    }
}

void writeLiteral(int fd, char const* text) {
    writeAll(fd, text, std::strlen(text));
}

void writeNumber(int fd, unsigned int value) {
    char buffer[16] = {};
    int index = 15;
    do {
        buffer[--index] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value && index > 0);
    writeAll(fd, buffer + index, static_cast<size_t>(15 - index));
}

void writeSignedNumber(int fd, int value) {
    if (value < 0) {
        writeLiteral(fd, "-");
        writeNumber(fd, static_cast<unsigned int>(
                            -static_cast<long long>(value)));
        return;
    }
    writeNumber(fd, static_cast<unsigned int>(value));
}

void writeAddress(int fd, uintptr_t value) {
    constexpr char hex[] = "0123456789abcdef";
    char address[2 + sizeof(uintptr_t) * 2] = {'0', 'x'};
    for (size_t i = 0; i < sizeof(uintptr_t) * 2; ++i) {
        const size_t shift = (sizeof(uintptr_t) * 2 - i - 1) * 4;
        address[2 + i] = hex[(value >> shift) & 0xf];
    }
    writeAll(fd, address, sizeof(address));
}

uintptr_t instructionPointer(void* context) {
#if defined(__aarch64__) && defined(__APPLE__)
    auto* uc = static_cast<ucontext_t*>(context);
    return uc && uc->uc_mcontext ? uc->uc_mcontext->__ss.__pc : 0;
#elif defined(__aarch64__) && defined(__ANDROID__)
    auto* uc = static_cast<ucontext_t*>(context);
    return uc ? uc->uc_mcontext.pc : 0;
#else
    (void)context;
    return 0;
#endif
}

void writeContext(int fd, void* context) {
#if defined(__aarch64__) && defined(__APPLE__)
    auto* uc = static_cast<ucontext_t*>(context);
    if (!uc || !uc->uc_mcontext) return;
    writeLiteral(fd, "\nPC: ");
    writeAddress(fd, uc->uc_mcontext->__ss.__pc);
    writeLiteral(fd, "\nLR: ");
    writeAddress(fd, uc->uc_mcontext->__ss.__lr);
    writeLiteral(fd, "\nSP: ");
    writeAddress(fd, uc->uc_mcontext->__ss.__sp);
#elif defined(__aarch64__) && defined(__ANDROID__)
    auto* uc = static_cast<ucontext_t*>(context);
    if (!uc) return;
    writeLiteral(fd, "\nPC: ");
    writeAddress(fd, uc->uc_mcontext.pc);
    writeLiteral(fd, "\nLR: ");
    writeAddress(fd, uc->uc_mcontext.regs[30]);
    writeLiteral(fd, "\nSP: ");
    writeAddress(fd, uc->uc_mcontext.sp);
#else
    (void)fd;
    (void)context;
#endif
}

#ifdef __ANDROID__
uintptr_t parseHex(char const* begin, char const* end) {
    uintptr_t value = 0;
    for (auto* cursor = begin; cursor < end; ++cursor) {
        const char c = *cursor;
        const unsigned digit =
            c >= '0' && c <= '9' ? static_cast<unsigned>(c - '0') :
            c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a' + 10) :
            c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A' + 10) : 16;
        if (digit > 15) break;
        value = value * 16 + digit;
    }
    return value;
}
#endif

void appendAddressMap(int fd, uintptr_t address) {
#ifdef __ANDROID__
    if (!address) return;
    const int maps = ::open("/proc/self/maps", O_RDONLY);
    if (maps < 0) return;

    char input[4096];
    char line[1024];
    size_t lineSize = 0;
    bool found = false;
    ssize_t size = 0;
    while (!found && (size = ::read(maps, input, sizeof(input))) > 0) {
        for (ssize_t i = 0; i < size; ++i) {
            if (input[i] != '\n' && lineSize < sizeof(line) - 1) {
                line[lineSize++] = input[i];
                continue;
            }
            const char* dash = line;
            while (dash < line + lineSize && *dash != '-') ++dash;
            const uintptr_t start = parseHex(line, dash);
            const uintptr_t end =
                dash < line + lineSize ? parseHex(dash + 1, line + lineSize)
                                       : 0;
            if (address >= start && address < end) {
                writeLiteral(fd, "\nModule offset: ");
                writeAddress(fd, address - start);
                writeLiteral(fd, "\nFaulting module: ");
                writeAll(fd, line, lineSize);
                found = true;
                break;
            }
            lineSize = 0;
        }
    }
    ::close(maps);
#else
    (void)fd;
    (void)address;
#endif
}

void writeBreadcrumbs(int fd) {
    writeLiteral(fd, "\n");
    writeLiteral(fd, g_header);
    writeLiteral(fd, "\nRecent activity (oldest first):");
    bool any = false;
    forEachBreadcrumb([&](const char* line) {
        writeLiteral(fd, "\n  ");
        writeLiteral(fd, line);
        any = true;
    });
    if (!any) writeLiteral(fd, "\n  (none recorded)");
}

void mobileSignalHandler(int signal, siginfo_t* info, void* context) {
    if (g_handlingSignal) _exit(128 + signal);
    g_handlingSignal = 1;

    writeLiteral(STDERR_FILENO, "\n=== Grape mobile crash ===\nSignal: ");
    writeNumber(STDERR_FILENO, static_cast<unsigned int>(signal));
    writeContext(STDERR_FILENO, context);
    writeBreadcrumbs(STDERR_FILENO);
    writeLiteral(STDERR_FILENO, "\n");

    const int fd = ::open(g_mobileCrashPath, O_WRONLY | O_CREAT | O_APPEND,
                          S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        writeLiteral(fd, "\n=== Grape mobile crash ===\nSignal: ");
        writeNumber(fd, static_cast<unsigned int>(signal));
        writeLiteral(fd, "\nSignal code: ");
        writeSignedNumber(fd, info ? info->si_code : 0);
        writeLiteral(fd, "\nFault address: ");
        if (info && info->si_addr) {
            writeAddress(fd, reinterpret_cast<uintptr_t>(info->si_addr));
        } else {
            writeLiteral(fd, "unknown");
        }
        writeContext(fd, context);
        writeBreadcrumbs(fd);
        appendAddressMap(fd, instructionPointer(context));
        writeLiteral(fd, "\n");
        writeLiteral(fd, "=== end crash ===\n");
        ::fsync(fd);
        ::close(fd);
    }

    ::sigaction(signal, &g_previousHandlers[signal], nullptr);
    ::kill(::getpid(), signal);
    _exit(128 + signal);
}

void mobileTerminateHandler() {
    const int fd = ::open(g_mobileCrashPath, O_WRONLY | O_CREAT | O_APPEND,
                          S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        writeLiteral(fd, "\n=== Grape std::terminate ===");
        if (std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (std::exception const& error) {
                writeLiteral(fd, "\nException: ");
                writeLiteral(fd, error.what());
            } catch (...) {
                writeLiteral(fd, "\nException: non-std::exception type");
            }
        } else {
            writeLiteral(fd, "\nException: none (direct terminate call)");
        }
        writeBreadcrumbs(fd);
        writeLiteral(fd, "\n=== end terminate ===\n");
        ::fsync(fd);
        ::close(fd);
    }
    std::abort();
}
}

void crash_log::install() {
#ifdef GEODE_IS_IOS
    auto directory =
        grape::paths::toStd(geode::dirs::getSaveDir()).parent_path() / "Grape" / "logs";
    std::filesystem::create_directories(directory);
    auto path = directory / "mobile-last-crash.log";
#else
    auto path = grape::paths::directory("logs") / "mobile-last-crash.log";
#endif
    captureHeader();
    auto pathString = path.string();
    std::strncpy(g_mobileCrashPath, pathString.c_str(),
                 sizeof(g_mobileCrashPath) - 1);

    std::ofstream session(path, std::ios::out | std::ios::app);
    session << "\nGrape mobile session started\n";
    session.flush();
    geode::log::info("Grape crash log: {}", path);
    installSessionMarker();

    struct sigaction action = {};
    action.sa_sigaction = mobileSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (int signal : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGTRAP}) {
        sigaction(signal, &action, &g_previousHandlers[signal]);
    }
    std::set_terminate(mobileTerminateHandler);
}

void crash_log::breadcrumb(std::string_view message) {
    recordBreadcrumb(message);
}

#endif
