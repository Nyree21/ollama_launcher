#include "single_instance.h"

#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>

SingleInstance::SingleInstance(const QString &channel, QObject *parent)
    : QObject(parent), m_serverName(buildServerName(channel))
{
    // Probe first: on Windows a second process can also listen on the same pipe
    // name, so the return value of listen() alone cannot tell primary/secondary.
    {
        QLocalSocket probe(this);
        probe.connectToServer(m_serverName, QIODevice::WriteOnly);
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            m_primary = false;
            return;
        }
    }

    m_server = new QLocalServer(this);
    if (m_server->listen(m_serverName)) {
        m_primary = true;
        connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
        return;
    }

#ifndef Q_OS_WIN
    // A crash can leave the socket file behind on Unix; clean up and retry once.
    QLocalServer::removeServer(m_serverName);
    if (m_server->listen(m_serverName)) {
        m_primary = true;
        connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
        return;
    }
#endif
    m_primary = false;
}

SingleInstance::~SingleInstance()
{
    if (m_server) m_server->close();
}

QString SingleInstance::buildServerName(const QString &channel) const
{
    // Include the user name so different Windows sessions never share a pipe.
    QString identity = QStringLiteral("ollama_launcher_");
    const QByteArray user = qgetenv("USERNAME").isEmpty() ? qgetenv("USER") : qgetenv("USERNAME");
    identity += QString::fromLocal8Bit(user.isEmpty() ? "default" : user.constData());
    const QByteArray hash =
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("OllamaLauncher_") + channel + QLatin1Char('_')
           + QString::fromLatin1(hash.left(12));
}

bool SingleInstance::sendShowRequest()
{
    return sendPayload("SHOW\n");
}

bool SingleInstance::sendQuitRequest()
{
    return sendPayload("QUIT\n");
}

bool SingleInstance::sendPayload(const QByteArray &payload)
{
    QLocalSocket socket(this);
    socket.connectToServer(m_serverName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(1500)) return false;
    if (socket.write(payload) != payload.size()) return false;
    if (!socket.waitForBytesWritten(1500)) return false;

    // Wait for the one-byte acknowledgement so this process does not exit
    // before the primary instance has consumed the command.
    socket.flush();
    if (socket.waitForReadyRead(1500)) socket.readAll();
    return true;
}

void SingleInstance::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *peer = m_server->nextPendingConnection();
        connect(peer, &QLocalSocket::readyRead, this, &SingleInstance::onSocketReadyRead);
        connect(peer, &QLocalSocket::disconnected, peer, &QObject::deleteLater);
    }
}

void SingleInstance::onSocketReadyRead()
{
    auto *peer = qobject_cast<QLocalSocket *>(sender());
    if (!peer) return;

    const QByteArray data = peer->readAll();
    bool handled = false;
    if (data.contains("SHOW")) { emit showRequested(); handled = true; }
    if (data.contains("QUIT")) { emit quitRequested(); handled = true; }
    if (handled) peer->write("K"); // acknowledgement
}
