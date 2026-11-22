#include "ollama_monitor.h"

#include "i18n.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QSet>
#include <QUrl>

namespace
{
constexpr double kMb = 1024.0 * 1024.0;
}

OllamaMonitor::OllamaMonitor(const LauncherConfig &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg)
{
}

void OllamaMonitor::setConfig(const LauncherConfig &cfg)
{
    m_cfg = cfg;
}

QString OllamaMonitor::baseUrl() const
{
    return QStringLiteral("http://%1:%2").arg(m_cfg.connectHost()).arg(m_cfg.port);
}

QNetworkReply *OllamaMonitor::getJson(const QString &path)
{
    QNetworkRequest request(QUrl(baseUrl() + path));
    request.setTransferTimeout(4000);
    return m_nam.get(request);
}

QString OllamaMonitor::replyError(QNetworkReply *reply) const
{
    if (reply->error() != QNetworkReply::NoError) return reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300) return QStringLiteral("HTTP %1").arg(status);
    return QString();
}

void OllamaMonitor::refresh()
{
    if (m_pending > 0) return;

    m_snap = MonitorSnapshot();

    // Local data is gathered even when the API is down, so "service not started"
    // can be told apart from "process alive but API stuck".
    ProcessMemory memory;
    if (m_pid != 0 && WinProcessInfo::processMemory(m_pid, &memory)) {
        m_snap.processWorkingSetMb = memory.workingSetMb;
        m_snap.processPrivateMb = memory.privateMb;
    }

    QSet<QPair<quint32, quint16>> seen;
    auto appendClients = [this, &seen](const QVector<TcpClient> &list) {
        for (const TcpClient &client : list) {
            const auto key = qMakePair(client.pid, client.remotePort);
            if (seen.contains(key)) continue;
            seen.insert(key);
            m_snap.clients.append(client);
        }
    };
    appendClients(WinProcessInfo::tcpClients(m_cfg.port));
    if (m_cfg.proxyEnabled && m_cfg.proxyPort != m_cfg.port)
        appendClients(WinProcessInfo::tcpClients(m_cfg.proxyPort));

    m_pending = 3;

    QNetworkReply *version = getJson(QStringLiteral("/api/version"));
    connect(version, &QNetworkReply::finished, this, &OllamaMonitor::onVersionFinished);

    QNetworkReply *ps = getJson(QStringLiteral("/api/ps"));
    connect(ps, &QNetworkReply::finished, this, &OllamaMonitor::onPsFinished);

    QNetworkReply *tags = getJson(QStringLiteral("/api/tags"));
    connect(tags, &QNetworkReply::finished, this, &OllamaMonitor::onTagsFinished);
}

void OllamaMonitor::maybeFinish()
{
    if (m_pending > 0) return;
    m_snap.updatedAt = QDateTime::currentDateTime();
    emit snapshotReady(m_snap);
}

void OllamaMonitor::onVersionFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    const QString error = replyError(reply);
    if (error.isEmpty()) {
        m_snap.reachable = true;
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_snap.version = obj.value(QStringLiteral("version")).toString();
    } else if (m_snap.error.isEmpty()) {
        m_snap.error = error;
    }

    reply->deleteLater();
    --m_pending;
    maybeFinish();
}

void OllamaMonitor::onPsFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    const QString error = replyError(reply);
    if (error.isEmpty()) {
        m_snap.reachable = true;
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray models = obj.value(QStringLiteral("models")).toArray();
        for (const QJsonValue &value : models) {
            const QJsonObject model = value.toObject();
            RunningModel running;
            running.name = model.value(QStringLiteral("name")).toString();
            running.sizeMb = model.value(QStringLiteral("size")).toDouble() / kMb;
            running.vramMb = model.value(QStringLiteral("size_vram")).toDouble() / kMb;
            running.expiresAt = model.value(QStringLiteral("expires_at")).toString();
            m_snap.models.append(running);
            m_snap.modelVramMb += running.vramMb;
        }
        m_snap.runningModels = m_snap.models.size();
    } else if (m_snap.error.isEmpty()) {
        m_snap.error = error;
    }

    reply->deleteLater();
    --m_pending;
    maybeFinish();
}

void OllamaMonitor::onTagsFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    const QString error = replyError(reply);
    if (error.isEmpty()) {
        m_snap.reachable = true;
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray models = obj.value(QStringLiteral("models")).toArray();
        for (const QJsonValue &value : models) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) m_snap.installedNames.append(name);
        }
        m_snap.installedModels = models.size();
    } else if (m_snap.error.isEmpty()) {
        m_snap.error = error;
    }

    reply->deleteLater();
    --m_pending;
    maybeFinish();
}

void OllamaMonitor::probeTtft(const QString &model)
{
    if (m_probeReply) return;
    if (model.trimmed().isEmpty()) {
        emit probeFinished(0.0, 0, 0.0, T("No model available to benchmark"));
        return;
    }

    QNetworkRequest request(QUrl(baseUrl() + QStringLiteral("/api/generate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(30000);

    QJsonObject options;
    options.insert(QStringLiteral("num_predict"), 8); // a few tokens give a usable tok/s

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("prompt"), QStringLiteral("hi"));
    body.insert(QStringLiteral("stream"), true);
    // Thinking models would emit a long reasoning block first, which inflates
    // TTFT and can hit the timeout before the first answer token.
    body.insert(QStringLiteral("think"), false);
    body.insert(QStringLiteral("options"), options);

    m_probeBuffer.clear();
    m_probeLastLine.clear();
    m_probeTtftMs = 0.0;
    m_probeReply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_probeTimer.start();
    connect(m_probeReply, &QNetworkReply::readyRead, this, &OllamaMonitor::onProbeReadyRead);
    connect(m_probeReply, &QNetworkReply::finished, this, &OllamaMonitor::onProbeFinished);
}

void OllamaMonitor::abortProbe()
{
    if (!m_probeReply) return;
    QNetworkReply *reply = m_probeReply;
    m_probeReply = nullptr; // onProbeFinished() returns early, no duplicate signal
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void OllamaMonitor::onProbeReadyRead()
{
    if (!m_probeReply) return;
    m_probeBuffer.append(m_probeReply->readAll());

    int nl = m_probeBuffer.indexOf('\n');
    while (nl >= 0) {
        const QByteArray line = m_probeBuffer.left(nl).trimmed();
        m_probeBuffer.remove(0, nl + 1);
        if (!line.startsWith('{')) {
            nl = m_probeBuffer.indexOf('\n');
            continue;
        }
        // Frames are consumed here, so the last one must be kept: the final
        // frame carries eval_count / prompt_eval_duration and may arrive in the
        // same read as the previous one.
        m_probeLastLine = line;
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (m_probeTtftMs == 0.0 && !obj.value(QStringLiteral("token")).toString().isEmpty())
            m_probeTtftMs = static_cast<double>(m_probeTimer.elapsed());
        nl = m_probeBuffer.indexOf('\n');
    }
}

void OllamaMonitor::onProbeFinished()
{
    if (!m_probeReply) return;
    QNetworkReply *reply = m_probeReply;
    m_probeReply = nullptr;

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString error = replyError(reply);
    QByteArray raw;
    if (error.isEmpty()) {
        raw = reply->readAll();
        m_probeBuffer.append(raw);
    }

    // Parse the remaining NDJSON, starting with the retained frame, and pick up
    // an "error" object if the server reported one.
    QJsonObject last;
    QString jsonError;
    auto absorb = [&](const QByteArray &chunk) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(chunk, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return; // never let a half frame overwrite a valid one
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error")))
            jsonError = obj.value(QStringLiteral("error")).toString();
        last = obj;
    };

    if (!m_probeLastLine.isEmpty()) absorb(m_probeLastLine);
    int nl = m_probeBuffer.indexOf('\n');
    while (nl >= 0) {
        const QByteArray line = m_probeBuffer.left(nl).trimmed();
        m_probeBuffer.remove(0, nl + 1);
        if (line.startsWith('{')) absorb(line);
        nl = m_probeBuffer.indexOf('\n');
    }
    if (m_probeBuffer.trimmed().startsWith('{')) absorb(m_probeBuffer.trimmed());

    if (error.isEmpty() && !jsonError.isEmpty())
        error = T("Service returned an error: ") + jsonError;

    const qint64 tokens = static_cast<qint64>(last.value(QStringLiteral("eval_count")).toInt());
    const double evalSec = last.value(QStringLiteral("eval_duration")).toDouble() / 1e9;

    // A tiny eval_duration (say 1 us) would make tok/s explode; require 1 ms.
    const double tokensPerSec = (evalSec >= 0.001 && tokens > 0)
        ? static_cast<double>(tokens) / evalSec
        : (tokens > 0 ? 1000.0 : 0.0);

    double ttft = m_probeTtftMs;
    // Streaming models may leave the first token empty; the server-reported
    // prompt_eval_duration is then the better estimate.
    const double promptEvalMs = last.value(QStringLiteral("prompt_eval_duration")).toDouble() / 1e6;
    if (promptEvalMs > 0.0) ttft = promptEvalMs;
    if (ttft <= 0.0) ttft = m_probeTtftMs;

    // Report a diagnosable reason instead of leaving the UI stuck on
    // "Benchmarking..." with a silent zero result.
    if (error.isEmpty() && tokens <= 0 && ttft <= 0.0) {
        const QString preview = QString::fromUtf8(raw.left(200)).simplified();
        error = T("No valid tokens received (HTTP %1, %2 bytes)%3")
                    .arg(httpStatus)
                    .arg(raw.size())
                    .arg(preview.isEmpty() ? QString() : QStringLiteral(": ") + preview);
    }

    reply->deleteLater();
    emit probeFinished(ttft, tokens, tokensPerSec, error);
}
