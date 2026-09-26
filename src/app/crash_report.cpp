#include "crash_report.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>

namespace d2x {
namespace {
wchar_t reportRoot[MAX_PATH]{};
wchar_t executablePath[MAX_PATH]{};
LONG reporting = 0;
bool symbolsReady = false;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;

void writeText(HANDLE file, const char *format, ...) noexcept {
    if (file == INVALID_HANDLE_VALUE) return;
    char buffer[2048];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length <= 0) return;
    DWORD written = 0;
    WriteFile(file, buffer, DWORD(length < int(sizeof(buffer)) ? length : sizeof(buffer) - 1),
              &written, nullptr);
}

void writeFrame(HANDLE file, unsigned index, DWORD64 address) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    wchar_t modulePath[MAX_PATH]{};
    char moduleName[MAX_PATH * 3]{};
    DWORD64 base = 0;
    if (VirtualQuery(reinterpret_cast<const void *>(address), &memory, sizeof(memory))) {
        base = reinterpret_cast<DWORD64>(memory.AllocationBase);
        if (GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), modulePath, MAX_PATH)) {
            const auto *name = std::wcsrchr(modulePath, L'\\');
            WideCharToMultiByte(CP_UTF8, 0, name ? name + 1 : modulePath, -1,
                                moduleName, sizeof(moduleName), nullptr, nullptr);
        }
    }
    writeText(file, "#%02u 0x%016llx %s+0x%llx", index,
              static_cast<unsigned long long>(address), moduleName[0] ? moduleName : "unknown",
              static_cast<unsigned long long>(address - base));
    if (symbolsReady) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol))
            writeText(file, " %s+0x%llx", symbol->Name, static_cast<unsigned long long>(displacement));
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(GetCurrentProcess(), address, &lineDisplacement, &line))
            writeText(file, " (%s:%lu)", line.FileName, line.LineNumber);
    }
    writeText(file, "\r\n");
    FlushFileBuffers(file);
}

void writeStack(HANDLE file, CONTEXT context) noexcept {
    STACKFRAME64 frame{};
    DWORD machine = 0;
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrFrame.Offset = context.Ebp;
#else
    writeText(file, "Text stack unwinding is unavailable for this architecture; use the dump.\r\n");
    return;
#endif
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    writeFrame(file, 0, frame.AddrPC.Offset);
    if (!symbolsReady) return;
    DWORD64 lastAddress = frame.AddrPC.Offset, lastStack = frame.AddrStack.Offset;
    for (unsigned index = 1; index < 64; ++index) {
        if (!StackWalk64(machine, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !frame.AddrPC.Offset)
            break;
        if (frame.AddrPC.Offset == lastAddress && frame.AddrStack.Offset == lastStack) break;
        writeFrame(file, index, frame.AddrPC.Offset);
        lastAddress = frame.AddrPC.Offset;
        lastStack = frame.AddrStack.Offset;
    }
}

void report(EXCEPTION_POINTERS *exception, const char *reason) noexcept {
    if (!reportRoot[0] || InterlockedCompareExchange(&reporting, 1, 0)) return;
    SYSTEMTIME now{};
    GetSystemTime(&now);
    wchar_t directory[MAX_PATH]{};
    const int length = std::swprintf(directory, MAX_PATH,
        L"%ls\\%04u%02u%02u-%02u%02u%02u-%03u-%lu-%lu", reportRoot,
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId());
    if (length <= 0 || length > MAX_PATH - 32 || !CreateDirectoryW(directory, nullptr)) return;
    wchar_t path[MAX_PATH]{};
    std::swprintf(path, MAX_PATH, L"%ls\\crash.txt", directory);
    const HANDLE text = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    writeText(text, "D2X crash report (UTC)\r\n%s\r\nProcess=%lu Thread=%lu\r\n",
              reason, GetCurrentProcessId(), GetCurrentThreadId());
    writeText(text, "Exception=0x%08lx Address=%p ImageBase=%p\r\n",
              exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress,
              GetModuleHandleW(nullptr));
    if (exception->ExceptionRecord->ExceptionCode == 0xE000D2A0)
        writeText(text, "Context=reporting handler; C++ stack may already be unwound. Not the original throw context.\r\n");
    writeText(text, "Dump and text are best effort. Forced termination, fail-fast and severe corruption may bypass reporting.\r\n");
    if (text != INVALID_HANDLE_VALUE) FlushFileBuffers(text);

    std::swprintf(path, MAX_PATH, L"%ls\\crash.dmp", directory);
    const HANDLE dump = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        const bool saved = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
            MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
            &info, nullptr, nullptr) != FALSE;
        const DWORD error = saved ? ERROR_SUCCESS : GetLastError();
        FlushFileBuffers(dump);
        CloseHandle(dump);
        writeText(text, "Minidump=%s Win32Error=%lu\r\n", saved ? "written" : "failed", error);
    } else writeText(text, "Minidump=open failed Win32Error=%lu\r\n", GetLastError());

    std::swprintf(path, MAX_PATH, L"%ls\\d2x.exe", directory);
    const bool copied = executablePath[0] && CopyFileW(executablePath, path, TRUE);
    writeText(text, "MatchingExecutable=%s\r\n", copied ? "copied" : "copy failed");
    wchar_t pdbPath[MAX_PATH]{};
    std::wcsncpy(pdbPath, executablePath, MAX_PATH - 1);
    if (auto *extension = std::wcsrchr(pdbPath, L'.'); extension && extension - pdbPath < MAX_PATH - 5) {
        std::wcscpy(extension, L".pdb");
        std::swprintf(path, MAX_PATH, L"%ls\\d2x.pdb", directory);
        CopyFileW(pdbPath, path, TRUE);
    }
    writeText(text, "Stack (symbols/line numbers depend on the build; module offsets remain usable with d2x.exe):\r\n");
    if (text != INVALID_HANDLE_VALUE) FlushFileBuffers(text);
    writeStack(text, *exception->ContextRecord);
    if (text != INVALID_HANDLE_VALUE) CloseHandle(text);
}

LONG WINAPI unhandledException(EXCEPTION_POINTERS *exception) {
    report(exception, "Unhandled native exception; stack starts at the fault context.");
    return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

void writeFatalErrorReport(const char *message) noexcept {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xE000D2A0;
#if defined(_M_X64) || defined(__x86_64__)
    record.ExceptionAddress = reinterpret_cast<void *>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
    record.ExceptionAddress = reinterpret_cast<void *>(context.Eip);
#endif
    EXCEPTION_POINTERS exception{&record, &context};
    report(&exception, message ? message : "Fatal C++ error; captured at the handler, not at the original throw.");
}

void installCrashReporting() noexcept {
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (pathLength >= MAX_PATH) executablePath[0] = L'\0';
    CreateDirectoryW(L"artifacts", nullptr);
    if (!CreateDirectoryW(L"artifacts\\crashes", nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    const DWORD length = GetFullPathNameW(L"artifacts\\crashes", MAX_PATH, reportRoot, nullptr);
    if (!length || length >= MAX_PATH - 80) { reportRoot[0] = L'\0'; return; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES |
                  SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    symbolsReady = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    ULONG stackReserve = 64 * 1024;
    SetThreadStackGuarantee(&stackReserve);
    previousFilter = SetUnhandledExceptionFilter(unhandledException);
    std::set_terminate([] {
        writeFatalErrorReport("std::terminate; stack captured at terminate handler, not necessarily at original throw.");
        std::abort();
    });
}
} // namespace d2x
#else
namespace d2x {
void installCrashReporting() noexcept {}
void writeFatalErrorReport(const char *) noexcept {}
} // namespace d2x
#endif