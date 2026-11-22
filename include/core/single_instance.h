// Single-instance guard built on QLocalServer / QLocalSocket (named pipes on
// Windows). The first process listens and owns the UI; later processes connect,
// ask the first one to show itself and exit immediately.
#ifndef OL_CORE_SINGLE_INSTANCE_H
#define OL_CORE_SINGLE_INSTANCE_H

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString &channel, QObject *parent = nullptr);
    ~SingleInstance() override;

    bool isPrimary() const { return m_primary; }

    // Secondary process: ask the primary instance to show its window.
    bool sendShowRequest();

    // Secondary process: ask the primary instance to quit gracefully.
    bool sendQuitRequest();

signals:
    void showRequested();
    void quitRequested();

private slots:
    void onNewConnection();
    void onSocketReadyRead();

private:
    QString buildServerName(const QString &channel) const;
    bool sendPayload(const QByteArray &payload);

    QLocalServer *m_server = nullptr;
    QString m_serverName;
    bool m_primary = false;
};

#endif // OL_CORE_SINGLE_INSTANCE_H
