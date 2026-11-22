// Local HTTP stats proxy.
//
// Ollama has no "total generated tokens" or "global TTFT" endpoint, but every
// generate response carries eval_count / prompt_eval_duration. Clients that
// connect to this proxy are forwarded to Ollama unchanged, while the streamed
// NDJSON is parsed on the way back to accumulate real statistics — no patching
// of Ollama and no synthetic requests polluting the numbers.
//
//   client -> QTcpServer(proxyPort) -> ProxyConnection -> ollama(host:port)
#ifndef OL_PROXY_STATS_PROXY_H
#define OL_PROXY_STATS_PROXY_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTcpServer>

#include "app_config.h"
#include "ollama_process.h"

class QNetworkReply;
class QTcpSocket;
class StatsProxy;

struct ProxyStats
{
    quint64 requests = 0;
    quint64 promptTokens = 0;
    quint64 generatedTokens = 0;
    double lastTtftMs = 0.0;
    double avgTtftMs = 0.0;
    double lastTokensPerSec = 0.0;
    int active = 0;
    QDateTime startedAt;
};

// One forwarded request. The connection is closed after the response, which
// keeps the accounting of active clients simple.
class ProxyConnection : public QObject
{
    Q_OBJECT

public:
    ProxyConnection(QTcpSocket *client, const LauncherConfig &cfg,
                    QNetworkAccessManager *nam, StatsProxy *owner,
                    QObject *parent = nullptr);

signals:
    void sampleReady(bool hadTokens, double ttftMs, qint64 promptTokens,
                     qint64 genTokens, double tokensPerSec);
    void logMessage(OllamaProcess::LogLevel level, const QString &text);

private slots:
    void onClientReadyRead();
    void onClientDisconnected();
    void onReplyReadyRead();
    void onReplyFinished();

private:
    void dispatch(const QByteArray &body);
    void writeReplyHeaders();
    void parseResponse(const QByteArray &chunk, bool final);
    void handleJsonLine(const QByteArray &line);
    void failClient();

    QTcpSocket *m_client = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    StatsProxy *m_owner = nullptr;
    const LauncherConfig m_cfg;

    QByteArray m_reqBuf;
    bool m_headerParsed = false;
    int m_contentLength = 0;
    QString m_method;
    QString m_path;
    QByteArray m_contentType;
    QByteArray m_authorization;

    bool m_replyHeaderWritten = false;
    QByteArray m_respBuf;
    QElapsedTimer m_timer;
    double m_ttftMs = 0.0;
    double m_promptEvalMs = 0.0; // TTFT estimate for non-streaming responses
    qint64 m_promptTokens = 0;
    qint64 m_genTokens = 0;
    double m_tokensPerSec = 0.0;
};

class StatsProxy : public QObject
{
    Q_OBJECT

public:
    explicit StatsProxy(const LauncherConfig &cfg, QObject *parent = nullptr);

    bool start(QString *error = nullptr);
    void stop();
    bool isRunning() const { return m_server.isListening(); }
    quint16 port() const { return m_cfg.proxyPort; }
    const ProxyStats &stats() const { return m_stats; }
    void resetStats();

signals:
    void statsChanged(const ProxyStats &stats);
    void logMessage(OllamaProcess::LogLevel level, const QString &text);

private slots:
    void onNewConnection();
    void onSample(bool hadTokens, double ttftMs, qint64 promptTokens,
                  qint64 genTokens, double tokensPerSec);

private:
    QTcpServer m_server;
    QNetworkAccessManager m_nam;
    LauncherConfig m_cfg;
    ProxyStats m_stats;
    double m_ttftSum = 0.0;
    int m_ttftCount = 0;
};

#endif // OL_PROXY_STATS_PROXY_H
