// Read-only Win32 helpers: process memory, and the ESTABLISHED TCP connections
// that point at the Ollama port together with the owning process.
#ifndef OL_PLATFORM_WIN_PROCESS_INFO_H
#define OL_PLATFORM_WIN_PROCESS_INFO_H

#include <QString>
#include <QVector>

struct ProcessMemory
{
    double workingSetMb = 0.0; // resident set, shared pages included
    double privateMb = 0.0;    // private committed bytes
};

struct TcpClient
{
    quint32 pid = 0;
    QString name;          // e.g. python.exe
    QString path;          // full executable path
    QString remoteAddress; // peer IP
    quint16 remotePort = 0;
};

namespace WinProcessInfo
{
bool processMemory(quint32 pid, ProcessMemory *out);
QString processName(quint32 pid);
QString processPath(quint32 pid);

// ESTABLISHED connections whose local port is `localPort` (host byte order).
QVector<TcpClient> tcpClients(quint16 localPort);

// Last Win32 error, for diagnostics.
QString lastError();
} // namespace WinProcessInfo

#endif // OL_PLATFORM_WIN_PROCESS_INFO_H
