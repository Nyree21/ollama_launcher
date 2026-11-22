#include "stats_proxy.h"

#include "i18n.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QUrl>

namespace
{
constexpr int kMaxRequestBytes = 64 * 1024 * 1024;
}

// ---------------------------------------------------------------------------
// ProxyConnection
// ---------------------------------------------------------------------------

ProxyConnection::ProxyConnection(QTcpSocket *client, const LauncherConfig &cfg,
                                 QNetworkAccessManager *nam, StatsProxy *owner,
                                 QObject *parent)
    : QObject(parent), m_client(client), m_nam(nam), m_owner(owner), m_cfg(cfg)
{
    m_client->setParent(this);
    connect(m_client, &QTcpSocket::readyRead, this, &ProxyConnection::onClientReadyRead);
    connect(m_client, &QTcpSocket::disconnected, this, &ProxyConnection::onClientDisconnected);
}

void ProxyConnection::onClientReadyRead()
{
    if (m_reply) return; // one request per connection
    m_reqBuf.append(m_client->readAll());
    if (m_reqBuf.size() > kMaxRequestBytes) {
        failClient();
        return;
    }

    if (!m_headerParsed) {
        const int headerEnd = m_reqBuf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        const QByteArray head = m_reqBuf.left(headerEnd);
        m_reqBuf.remove(0, headerEnd + 4);

        const QList<QByteArray> lines = head.split('\n');
        if (!lines.isEmpty()) {
            const QList<QByteArray> parts = lines.first().trimmed().split(' ');
            if (parts.size() >= 2) {
                m_method = QString::fromLatin1(parts.at(0));
                m_path = QString::fromLatin1(parts.at(1));
            }
        }
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon < 0) continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (key == "content-length") m_contentLength = value.toInt();
            else if (key == "content-type") m_contentType = value;
            else if (key == "authorization") m_authorization = value;
        }
        m_headerParsed = true;

        // Debug endpoint: current counters, never forwarded to Ollama.
        if (m_path == QLatin1String("/__stats") && m_owner) {
            const ProxyStats &stats = m_owner->stats();
            QJsonObject obj;
            obj.insert(QStringLiteral("requests"), static_cast<qint64>(stats.requests));
            obj.insert(QStringLiteral("promptTokens"), static_cast<qint64>(stats.promptTokens));
            obj.insert(QStringLiteral("generatedTokens"), static_cast<qint64>(stats.generatedTokens));
            obj.insert(QStringLiteral("lastTtftMs"), stats.lastTtftMs);
            obj.insert(QStringLiteral("avgTtftMs"), stats.avgTtftMs);
            obj.insert(QStringLiteral("lastTokensPerSec"), stats.lastTokensPerSec);
            obj.insert(QStringLiteral("active"), stats.active);

            const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            const QByteArray response =
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
            m_client->write(response);
            m_client->flush();
            m_client->disconnectFromHost();
            return;
        }

        // Answer CORS preflight locally instead of bothering Ollama.
        if (m_method == QLatin1String("OPTIONS")) {
            m_client->write("HTTP/1.1 204 No Content\r\n"
                            "Access-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                            "Connection: close\r\n\r\n");
            m_client->flush();
            m_client->disconnectFromHost();
            return;
        }
    }

    if (m_reqBuf.size() < m_contentLength) return;
    dispatch(m_reqBuf.left(m_contentLength));
}

void ProxyConnection::dispatch(const QByteArray &body)
{
    if (m_path.isEmpty() || !m_path.startsWith(QLatin1Char('/'))) {
        failClient();
        return;
    }

    const QString url = QStringLiteral("http://%1:%2%3")
                            .arg(m_cfg.connectHost())
                            .arg(m_cfg.port)
                            .arg(m_path);
    QNetworkRequest request{QUrl(url)};
    if (!m_contentType.isEmpty()) request.setRawHeader("Content-Type", m_contentType);
    if (!m_authorization.isEmpty()) request.setRawHeader("Authorization", m_authorization);
    // Streaming generations can run for minutes; the client owns the timeout.
    request.setTransferTimeout(0);

    m_timer.start();
    m_reply = m_nam->sendCustomRequest(request, m_method.toLatin1(), body);
    connect(m_reply, &QNetworkReply::readyRead, this, &ProxyConnection::onReplyReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ProxyConnection::onReplyFinished);
}

void ProxyConnection::writeReplyHeaders()
{
    if (m_replyHeaderWritten) return;

    int status = m_reply ? m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    if (status <= 0) status = 200;

    QByteArray reason = "OK";
    if (status == 404) reason = "Not Found";
    else if (status >= 500) reason = "Server Error";
    else if (status == 400 || status == 422) reason = "Bad Request";

    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    QByteArray contentType =
        m_reply ? m_reply->header(QNetworkRequest::ContentTypeHeader).toByteArray() : QByteArray();
    if (contentType.isEmpty()) contentType = "application/json";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Connection: close\r\n\r\n";

    m_client->write(out);
    m_replyHeaderWritten = true;
}

void ProxyConnection::onReplyReadyRead()
{
    if (!m_reply) return;
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) return;

    writeReplyHeaders();
    m_client->write(chunk);
    m_client->flush();
    parseResponse(chunk, false);
}

void ProxyConnection::onReplyFinished()
{
    if (!m_reply) return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const int code = status > 0 ? status : 502;
        const QByteArray message = QJsonDocument(QJsonObject{
            {QStringLiteral("error"), reply->errorString()}}).toJson(QJsonDocument::Compact);
        const QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " Bad Gateway\r\n"
                               "Content-Type: application/json\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Connection: close\r\n\r\n" + message;
        m_client->write(out);
        m_replyHeaderWritten = true;
        emit logMessage(OllamaProcess::LogLevel::Warn,
                        T("Proxy forward failed: ") + reply->errorString());
    } else {
        writeReplyHeaders();
        parseResponse(reply->readAll(), true);
    }

    m_client->flush();
    if (m_ttftMs <= 0.0 && m_promptEvalMs > 0.0) m_ttftMs = m_promptEvalMs;
    emit sampleReady(m_genTokens > 0, m_ttftMs, m_promptTokens, m_genTokens, m_tokensPerSec);
    m_client->disconnectFromHost();
    reply->deleteLater();
}

void ProxyConnection::parseResponse(const QByteArray &chunk, bool final)
{
    m_respBuf.append(chunk);
    int nl = m_respBuf.indexOf('\n');
    while (nl >= 0) {
        const QByteArray line = m_respBuf.left(nl).trimmed();
        m_respBuf.remove(0, nl + 1);
        handleJsonLine(line);
        nl = m_respBuf.indexOf('\n');
    }
    if (final) handleJsonLine(m_respBuf.trimmed());
}

void ProxyConnection::handleJsonLine(const QByteArray &line)
{
    if (!line.startsWith('{')) return;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) return;
    const QJsonObject obj = doc.object();

    const bool hasTokenField = obj.contains(QStringLiteral("token"));
    const bool hasFirstToken =
        hasTokenField && !obj.value(QStringLiteral("token")).toString().isEmpty();
    const qint64 evalCount = static_cast<qint64>(obj.value(QStringLiteral("eval_count")).toInt());

    // Only streaming responses give a real "time to first token". For
    // non-streaming ones fall back to Ollama's prompt_eval_duration.
    if (m_ttftMs <= 0.0 && hasFirstToken) m_ttftMs = static_cast<double>(m_timer.elapsed());
    if (obj.contains(QStringLiteral("prompt_eval_duration")))
        m_promptEvalMs = obj.value(QStringLiteral("prompt_eval_duration")).toDouble() / 1e6;

    if (obj.contains(QStringLiteral("prompt_eval_count"))) {
        m_promptTokens = qMax(m_promptTokens,
                              static_cast<qint64>(obj.value(QStringLiteral("prompt_eval_count")).toInt()));
    }
    if (obj.contains(QStringLiteral("eval_count"))) m_genTokens = qMax(m_genTokens, evalCount);

    const double evalSeconds = obj.value(QStringLiteral("eval_duration")).toDouble() / 1e9;
    if (evalSeconds > 0 && evalCount > 0)
        m_tokensPerSec = static_cast<double>(evalCount) / evalSeconds;
}

void ProxyConnection::onClientDisconnected()
{
    if (m_reply) m_reply->abort();
    // Connections are not reused, so active-client accounting drops immediately.
    deleteLater();
}

void ProxyConnection::failClient()
{
    m_client->write("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
                    "Connection: close\r\n\r\n{\"error\":\"bad request\"}");
    m_client->flush();
    m_client->disconnectFromHost();
}

// ---------------------------------------------------------------------------
// StatsProxy
// ---------------------------------------------------------------------------

StatsProxy::StatsProxy(const LauncherConfig &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg)
{
}

bool StatsProxy::start(QString *error)
{
    if (m_server.isListening()) return true;

    // The listen address is normally an IP literal; accept a host name such as
    // "localhost" as well by falling back to loopback.
    QHostAddress address(m_cfg.host);
    if (address.isNull()) address = QHostAddress(QHostAddress::LocalHost);

    if (!m_server.listen(address, m_cfg.proxyPort)) {
        if (error) {
            *error = T("Stats proxy failed to listen on %1:%2: %3")
                         .arg(m_cfg.host)
                         .arg(m_cfg.proxyPort)
                         .arg(m_server.errorString());
        }
        return false;
    }

    connect(&m_server, &QTcpServer::newConnection, this, &StatsProxy::onNewConnection);
    m_stats = ProxyStats();
    m_stats.startedAt = QDateTime::currentDateTime();
    m_ttftSum = 0.0;
    m_ttftCount = 0;
    emit statsChanged(m_stats);
    return true;
}

void StatsProxy::stop()
{
    if (m_server.isListening()) m_server.close();
}

void StatsProxy::resetStats()
{
    const int active = m_stats.active;
    m_stats = ProxyStats();
    m_stats.active = active;
    m_stats.startedAt = QDateTime::currentDateTime();
    m_ttftSum = 0.0;
    m_ttftCount = 0;
    emit statsChanged(m_stats);
}

void StatsProxy::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        auto *connection = new ProxyConnection(socket, m_cfg, &m_nam, this);
        connect(connection, &ProxyConnection::sampleReady, this, &StatsProxy::onSample);
        connect(connection, &ProxyConnection::logMessage, this, &StatsProxy::logMessage);

        ++m_stats.active;
        connect(connection, &QObject::destroyed, this, [this](QObject *) {
            m_stats.active = qMax(0, m_stats.active - 1);
            emit statsChanged(m_stats);
        });
    }
    emit statsChanged(m_stats);
}

void StatsProxy::onSample(bool hadTokens, double ttftMs, qint64 promptTokens,
                          qint64 genTokens, double tokensPerSec)
{
    ++m_stats.requests;
    m_stats.promptTokens += static_cast<quint64>(qMax<qint64>(0, promptTokens));

    if (hadTokens) {
        m_stats.generatedTokens += static_cast<quint64>(qMax<qint64>(0, genTokens));
        if (ttftMs > 0) {
            m_ttftSum += ttftMs;
            ++m_ttftCount;
            m_stats.avgTtftMs = m_ttftSum / m_ttftCount;
            m_stats.lastTtftMs = ttftMs;
        }
    }
    if (tokensPerSec > 0) m_stats.lastTokensPerSec = tokensPerSec;
    emit statsChanged(m_stats);
}
