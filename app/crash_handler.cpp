#include "crash_handler.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <cstdio>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#  include <dbghelp.h>
#else
#  include <csignal>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace {
constexpr int kMaxCrashFiles = 5;

// Cached at install() time so the signal handler never allocates.
QByteArray g_crashDirectoryPath;

QString crashDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/crashes");
}

void pruneOldCrashFiles()
{
    QDir directory(crashDirectory());
    const auto entries = directory.entryList(
        {QStringLiteral("nexpdf-*")}, QDir::Files, QDir::Time | QDir::Reversed);
    for (int index = 0; index < entries.size() - kMaxCrashFiles; ++index) {
        directory.remove(entries.at(index));
    }
}

#ifdef Q_OS_WIN

LPCWSTR toWide(const QString &text)
{
    static wchar_t buffer[MAX_PATH];
    const int size = text.toWCharArray(buffer);
    buffer[size] = L'\0';
    return buffer;
}

LONG WINAPI writeMiniDump(EXCEPTION_POINTERS *exception)
{
    const QString path = crashDirectory() + QStringLiteral("/nexpdf-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))
        + QStringLiteral(".dmp");
    const HANDLE file = CreateFileW(toWide(path), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#else

void writeSignalReport(const int signalNumber)
{
    // Async-signal-safe only: no Qt allocations, plain open/write/close on a
    // path cached at install() time.
    if (g_crashDirectoryPath.isEmpty()) {
        return;
    }
    char path[512];
    const int written = std::snprintf(path, sizeof(path), "%s/nexpdf-signal.log",
                                      g_crashDirectoryPath.constData());
    if (written <= 0) {
        return;
    }
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        return;
    }
    char line[128];
    const int length = std::snprintf(line, sizeof(line), "signal %d pid %ld\n",
                                     signalNumber, static_cast<long>(::getpid()));
    if (length > 0) {
        (void)::write(fd, line, static_cast<size_t>(length));
    }
    ::close(fd);
}

void crashSignalHandler(const int signalNumber)
{
    writeSignalReport(signalNumber);
    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

#endif
}

namespace nexpdf::crash {

void install()
{
    QDir().mkpath(crashDirectory());
    pruneOldCrashFiles();
#ifdef Q_OS_UNIX
    g_crashDirectoryPath = QDir::toNativeSeparators(crashDirectory()).toUtf8();
#endif
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(writeMiniDump);
#else
    for (const int signalNumber : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE}) {
        ::signal(signalNumber, crashSignalHandler);
    }
#endif
}

} // namespace nexpdf::crash
