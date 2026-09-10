#include "app/crashhandler.h"

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include "app/logger.h"
#include "app/settings.h"
#include "ui/appshell.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <csignal>
#include <cstdio>
#include <exception>

#ifndef APP_VERSION
#define APP_VERSION "0.0"
#endif

namespace {

QString g_crashDir;

const char *exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE";
    case 0xE06D7363:                      return "C++ exception";
    default:                              return "unknown";
    }
}

// The process is already dying: Win32 and fixed buffers only, no allocation.
void write(HANDLE file, const char *text) {
    DWORD written = 0;
    WriteFile(file, text, DWORD(strlen(text)), &written, nullptr);
}

void writeLine(HANDLE file, const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    write(file, buffer);
    write(file, "\r\n");
}

void timestamp(char *out, size_t size) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(out, size, "%04d%02d%02d-%02d%02d%02d",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

void writeMiniDump(const wchar_t *path, EXCEPTION_POINTERS *ex) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ex;
    info.ClientPointers = FALSE;

    const auto type = MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory |
                                    MiniDumpWithDataSegs | MiniDumpWithThreadInfo);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                      ex ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

void writeReport(const wchar_t *path, EXCEPTION_POINTERS *ex, const char *reason) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    char when[32];
    timestamp(when, sizeof(when));
    writeLine(file, "AoNami %s crash report", APP_VERSION);
    writeLine(file, "time      : %s", when);
    writeLine(file, "reason    : %s", reason);

    void *faultPc = nullptr;
    if (ex && ex->ExceptionRecord) {
        const auto *record = ex->ExceptionRecord;
        faultPc = record->ExceptionAddress;
        writeLine(file, "exception : 0x%08lX (%s)",
                  record->ExceptionCode, exceptionName(record->ExceptionCode));
        writeLine(file, "address   : 0x%p", record->ExceptionAddress);
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            // Win32 uses 0 read, 1 write, 8 DEP-execute. There is no 2.
            const ULONG_PTR op = record->ExceptionInformation[0];
            writeLine(file, "operation : %s of 0x%p",
                      op == 0 ? "read" : op == 1 ? "write" : op == 8 ? "execute" : "?",
                      (void *)record->ExceptionInformation[1]);
        }
    }
    writeLine(file, "");

    // MinGW emits no PDB, so record module+offset; scripts/symbolise.py decodes it.
    writeLine(file, "--- stack (module+offset; symbolise with addr2line) ---");
    void *frames[62];
    const USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        uintptr_t offset = 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)frames[i], &module) && module) {
            GetModuleFileNameA(module, name, MAX_PATH);
            offset = uintptr_t(frames[i]) - uintptr_t(module);
        }
        const char *base = strrchr(name, '\\');
        writeLine(file, "  [%02d] 0x%p  %s+0x%llx", i, frames[i],
                  base ? base + 1 : name, (unsigned long long)offset);
    }
    if (faultPc) writeLine(file, "  (faulting pc above is the exception address)");
    writeLine(file, "");

    writeLine(file, "--- modules ---");
    HMODULE modules[256];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        const DWORD n = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < n && i < 256; ++i) {
            char name[MAX_PATH] = "?";
            GetModuleFileNameA(modules[i], name, MAX_PATH);
            MODULEINFO mi{};
            GetModuleInformation(GetCurrentProcess(), modules[i], &mi, sizeof(mi));
            const char *base = strrchr(name, '\\');
            writeLine(file, "  0x%p +0x%08lx  %s", modules[i], mi.SizeOfImage,
                      base ? base + 1 : name);
        }
    }
    CloseHandle(file);
}

void buildPaths(wchar_t *report, wchar_t *dump, size_t size) {
    char when[32];
    timestamp(when, sizeof(when));
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    if (wchar_t *slash = wcsrchr(dir, L'\\')) *(slash + 1) = 0;
    wcscat_s(dir, MAX_PATH, L"crashes");
    CreateDirectoryW(dir, nullptr);
    swprintf(report, size, L"%s\\crash_%hs.txt", dir, when);
    swprintf(dump, size, L"%s\\crash_%hs.dmp", dir, when);
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS *ex) {
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1)) return EXCEPTION_EXECUTE_HANDLER;

    wchar_t report[MAX_PATH], dump[MAX_PATH];
    buildPaths(report, dump, MAX_PATH);
    writeReport(report, ex, "unhandled exception");
    writeMiniDump(dump, ex);
    return EXCEPTION_EXECUTE_HANDLER;
}

void onTerminate() {
    wchar_t report[MAX_PATH], dump[MAX_PATH];
    buildPaths(report, dump, MAX_PATH);

    const char *reason = "std::terminate";
    if (auto current = std::current_exception()) {
        try { std::rethrow_exception(current); }
        catch (const std::exception &e) {
            static char detail[512];
            snprintf(detail, sizeof(detail), "uncaught std::exception: %s", e.what());
            reason = detail;
        } catch (...) { reason = "uncaught non-std exception"; }
    }
    writeReport(report, nullptr, reason);
    writeMiniDump(dump, nullptr);
    _exit(3);
}

void onAbort(int) {
    wchar_t report[MAX_PATH], dump[MAX_PATH];
    buildPaths(report, dump, MAX_PATH);
    writeReport(report, nullptr, "abort()");
    writeMiniDump(dump, nullptr);
    _exit(3);
}

}

void CrashHandler::install() {
    SetUnhandledExceptionFilter(onUnhandled);
    // A stack overflow arrives on a full stack; reserve room for the filter to run.
    ULONG guarantee = 65536;
    SetThreadStackGuarantee(&guarantee);
    std::set_terminate(onTerminate);
    signal(SIGABRT, onAbort);
}

QString CrashHandler::crashDir() {
    if (g_crashDir.isEmpty())
        g_crashDir = QCoreApplication::applicationDirPath() + QStringLiteral("/crashes");
    return g_crashDir;
}

void CrashHandler::reportPending() {
    QDir dir(crashDir());
    if (!dir.exists()) return;

    const auto all = dir.entryInfoList({QStringLiteral("crash_*")}, QDir::Files, QDir::Time);
    for (int i = 20; i < all.size(); ++i)
        QFile::remove(all[i].absoluteFilePath());

    const auto reports = dir.entryInfoList({QStringLiteral("crash_*.txt")}, QDir::Files, QDir::Time);
    if (reports.isEmpty()) return;

    const QFileInfo &latest = reports.first();
    static const QString kSeenKey = QStringLiteral("crash/lastReported");
    Settings &settings = Settings::instance();
    if (settings.value(kSeenKey).toString() == latest.fileName()) return;
    settings.setValue(kSeenKey, latest.fileName());

    logError() << "Crash" << "Previous run crashed -" << latest.fileName();

    QFile file(latest.absoluteFilePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        for (int line = 0; line < 6 && !in.atEnd(); ++line) {
            const QString text = in.readLine().trimmed();
            if (!text.isEmpty()) logError() << "Crash" << text;
        }
    }

    AppShell::instance().reportError(
        QStringLiteral("AoNami closed unexpectedly last time.\nReport: %1").arg(latest.fileName()),
        QStringLiteral("Crash Report"));
}
