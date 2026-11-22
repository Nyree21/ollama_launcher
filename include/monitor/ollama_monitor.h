// Collects a runtime snapshot of the Ollama server: HTTP API data, process
// memory, live TCP clients, plus a short generation probe used to measure TTFT
// when the stats proxy is disabled.
#ifndef OL_MONITOR_OLLAMA_MONITOR_H
#define OL_MONITOR_OLLAMA_MONITOR_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVector>

#include "app_config.h"
#include "win_process_info.h"

class QNetworkReply;

struct RunningModel
{
    QString name;
    double sizeMb = 0.0;
    double vramMb = 0.0;
    QString expiresAt;
};

struct MonitorSnapshot
{
    bool reachable = false;
    QString error;              // reason when the API is unreachable or non-2xx
    QString version;
    int runningModels = 0;
    int installedModels = 0;
    QStringList installedNames; // for the benchmark model picker
    double processWorkingSetMb = 0.0;
    double processPrivateMb = 0.0;
    double modelVramMb = 0.0;
    QVector<RunningModel> models;
    QVector<TcpClient> clients;
    QDateTime updatedAt;
};

class OllamaMonitor : public QObject
{
    Q_OBJECT

public:
    explicit OllamaMonitor(const LauncherConfig &cfg, QObject *parent = nullptr);

    void setConfig(const LauncherConfig &cfg);
    void setProcessPid(qint64 pid) { m_pid = static_cast<quint32>(pid > 0 ? pid : 0); }

    // Async; a new round is skipped while the previous one is still running.
    void refresh();

    // Sends a very short generation request to the given model and measures the
    // time until the first token.
    void probeTtft(const QString &model);

    // Cancels an in-flight probe (service stopped or app quitting).
    void abortProbe();
    bool probeInFlight() const { return m_probeReply != nullptr; }

signals:
    void snapshotReady(const MonitorSnapshot &snapshot);
    void probeFinished(double ttftMs, qint64 tokens, double tokensPerSec, const QString &error);

private slots:
    void onVersionFinished();
    void onPsFinished();
    void onTagsFinished();
    void onProbeReadyRead();
    void onProbeFinished();

private:
    QNetworkReply *getJson(const QString &path);
    QString baseUrl() const;
    QString replyError(QNetworkReply *reply) const;
    void maybeFinish();

    QNetworkAccessManager m_nam;
    LauncherConfig m_cfg; // snapshot, so no dangling reference is possible
    quint32 m_pid = 0;
    int m_pending = 0;
    MonitorSnapshot m_snap;

    QNetworkReply *m_probeReply = nullptr;
    QElapsedTimer m_probeTimer;
    QByteArray m_probeBuffer;
    QByteArray m_probeLastLine; // last NDJSON frame, see onProbeFinished()
    double m_probeTtftMs = 0.0;
};

#endif // OL_MONITOR_OLLAMA_MONITOR_H
