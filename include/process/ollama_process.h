// Starts and stops `ollama serve` through QProcess and turns its console output
// into structured log lines.
#ifndef OL_PROCESS_OLLAMA_PROCESS_H
#define OL_PROCESS_OLLAMA_PROCESS_H

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

struct LauncherConfig;

// Launch command:
//   program = config.ollamaPath
//   args    = "serve" + splitCommand(config.extraArgs)
//   env     = OLLAMA_HOST / OLLAMA_MODELS (+ optional extras)
//
// stop() uses `taskkill /T` because QProcess::terminate() has no effect on a
// console process like ollama serve; killing it would otherwise be reported by
// Qt as a crash. m_stopRequested marks an intentional stop so the UI does not
// show a false "crashed" error.
class OllamaProcess : public QObject
{
    Q_OBJECT

public:
    enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };
    Q_ENUM(LogLevel)

    explicit OllamaProcess(QObject *parent = nullptr);

    // Takes effect on the next start(); does not launch anything.
    void configure(const LauncherConfig &cfg);

    bool start(QString *error = nullptr);
    void stop();

    // Blocks until the process is gone; used before the app quits so no orphan
    // ollama process keeps the port and VRAM.
    void stopAndWait();

    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
    qint64 pid() const { return isRunning() ? m_process.processId() : 0; }
    QString commandLine() const;

signals:
    void stateChanged(bool running);
    void logLine(OllamaProcess::LogLevel level, const QString &text);
    void processError(const QString &message);

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    static LogLevel classify(const QString &line);
    void emitBuffered(bool flushTail);
    void killProcessTree();
    void resetAfterStop();

    QProcess m_process;
    QString m_program;
    QStringList m_arguments;
    QProcessEnvironment m_environment;
    QByteArray m_buffer;          // partial line kept between reads
    bool m_stopRequested = false; // an intentional stop is in progress
};

#endif // OL_PROCESS_OLLAMA_PROCESS_H
