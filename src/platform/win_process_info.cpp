#include "win_process_info.h"

#include "i18n.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>

#include <QDir>
#include <QFileInfo>

namespace
{
thread_local QString g_lastError;

QString formatWinError(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    QString text = (length > 0 && buffer)
        ? QString::fromWCharArray(buffer, int(length))
        : QStringLiteral("Win32 error %1").arg(code);
    if (buffer) LocalFree(buffer);
    return text.trimmed();
}

HANDLE openForQuery(quint32 pid)
{
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!handle) g_lastError = formatWinError(GetLastError());
    return handle;
}
} // namespace

bool WinProcessInfo::processMemory(quint32 pid, ProcessMemory *out)
{
    if (!out || pid == 0) return false;
    HANDLE handle = openForQuery(pid);
    if (!handle) return false;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    const BOOL ok = GetProcessMemoryInfo(
        handle, reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters), sizeof(counters));
    const DWORD err = ok ? 0 : GetLastError();
    CloseHandle(handle);
    if (!ok) {
        g_lastError = formatWinError(err);
        return false;
    }

    constexpr double kMb = 1024.0 * 1024.0;
    out->workingSetMb = static_cast<double>(counters.WorkingSetSize) / kMb;
    out->privateMb = static_cast<double>(counters.PrivateUsage) / kMb;
    return true;
}

QString WinProcessInfo::processPath(quint32 pid)
{
    if (pid == 0) return QString();
    HANDLE handle = openForQuery(pid);
    if (!handle) return QString();

    wchar_t buffer[32768]{};
    DWORD size = DWORD(sizeof(buffer) / sizeof(buffer[0]));
    const BOOL ok = QueryFullProcessImageNameW(handle, 0, buffer, &size);
    const DWORD err = ok ? 0 : GetLastError();
    CloseHandle(handle);
    if (!ok) {
        g_lastError = formatWinError(err);
        return QString();
    }
    return QDir::toNativeSeparators(QString::fromWCharArray(buffer, int(size)));
}

QString WinProcessInfo::processName(quint32 pid)
{
    const QString path = processPath(pid);
    if (path.isEmpty()) return QStringLiteral("PID %1").arg(pid);
    return QFileInfo(path).fileName();
}

QVector<TcpClient> WinProcessInfo::tcpClients(quint16 localPort)
{
    QVector<TcpClient> result;

    ULONG size = 0;
    DWORD ret = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                    TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER) {
        g_lastError = formatWinError(ret);
        return result;
    }

    QByteArray buffer(int(size), Qt::Uninitialized);
    ret = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET,
                              TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (ret != NO_ERROR) {
        g_lastError = formatWinError(ret);
        return result;
    }

    auto *table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buffer.data());
    const DWORD wantedPort = htons(static_cast<USHORT>(localPort));

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID &row = table->table[i];
        // 5 == MIB_TCP_STATE_ESTAB; the numeric value avoids SDK name differences.
        if (row.dwState != 5 || row.dwLocalPort != wantedPort) continue;

        TcpClient client;
        client.pid = static_cast<quint32>(row.dwOwningPid);
        client.name = processName(client.pid);
        client.path = processPath(client.pid);

        struct in_addr addr{};
        addr.S_un.S_addr = row.dwRemoteAddr;
        char ip[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &addr, ip, sizeof(ip)))
            client.remoteAddress = QString::fromLatin1(ip);
        client.remotePort = ntohs(static_cast<USHORT>(row.dwRemotePort));
        result.append(client);
    }
    g_lastError.clear();
    return result;
}

QString WinProcessInfo::lastError()
{
    return g_lastError;
}

#else // !Q_OS_WIN

bool WinProcessInfo::processMemory(quint32, ProcessMemory *) { return false; }
QString WinProcessInfo::processPath(quint32) { return QString(); }
QString WinProcessInfo::processName(quint32 pid) { return QStringLiteral("PID %1").arg(pid); }
QVector<TcpClient> WinProcessInfo::tcpClients(quint16) { return {}; }
QString WinProcessInfo::lastError() { return T("Not supported on non-Windows platforms"); }

#endif
