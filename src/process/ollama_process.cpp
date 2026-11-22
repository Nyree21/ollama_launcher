#include "ollama_process.h"

#include "app_config.h"
#include "i18n.h"

#include <QRegularExpression>

OllamaProcess::OllamaProcess(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &OllamaProcess::onReadyRead);
    connect(&m_process, &QProcess::finished, this, &OllamaProcess::onFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &OllamaProcess::onErrorOccurred);
}

void OllamaProcess::configure(const LauncherConfig &cfg)
{
    m_program = cfg.ollamaPath;
    m_arguments.clear();
    m_arguments << QStringLiteral("serve");
    const QString extra = cfg.extraArgs.trimmed();
    if (!extra.isEmpty()) m_arguments << QProcess::splitCommand(extra);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OLLAMA_HOST"),
               cfg.host + QLatin1Char(':') + QString::number(cfg.port));
    if (!cfg.modelsDir.trimmed().isEmpty())
        env.insert(QStringLiteral("OLLAMA_MODELS"), cfg.modelsDir);

    // Without this, ollama contacts ollama.com on startup to hydrate its cloud
    // model list. Offline machines would log a stream of network errors.
    if (!env.contains(QStringLiteral("OLLAMA_NOCLOUD")))
        env.insert(QStringLiteral("OLLAMA_NOCLOUD"), QStringLiteral("1"));

    // [env] entries override or extend anything above.
    for (auto it = cfg.extraEnv.constBegin(); it != cfg.extraEnv.constEnd(); ++it) {
        if (!it.key().trimmed().isEmpty()) env.insert(it.key(), it.value());
    }
    m_environment = env;
}

bool OllamaProcess::start(QString *error)
{
    if (isRunning()) {
        if (error) *error = T("ollama serve is already running.");
        return false;
    }
    if (m_program.trimmed().isEmpty()) {
        if (error) *error = T("No ollama.exe path configured.");
        return false;
    }

    m_buffer.clear();
    m_stopRequested = false;
    m_process.setProgram(m_program);
    m_process.setArguments(m_arguments);
    m_process.setProcessEnvironment(m_environment);
    m_process.start();

    if (!m_process.waitForStarted(3000)) {
        const QString msg = T("Failed to start: %1").arg(m_process.errorString());
        if (error) *error = msg;
        emit processError(msg);
        emit logLine(LogLevel::Error, msg);
        return false;
    }

    emit logLine(LogLevel::Info,
                 T("Started ollama serve (PID=%1)").arg(m_process.processId()));
    emit logLine(LogLevel::Debug, commandLine());
    emit stateChanged(true);
    return true;
}

void OllamaProcess::stop()
{
    if (!isRunning()) return;
    m_stopRequested = true;
    emit logLine(LogLevel::Info,
                 T("Stopping ollama serve (PID=%1)...").arg(m_process.processId()));
    killProcessTree();
}

void OllamaProcess::stopAndWait()
{
    if (!isRunning()) return;
    m_stopRequested = true;
    killProcessTree();
    if (isRunning()) m_process.waitForFinished(3000);
    resetAfterStop();
}

void OllamaProcess::killProcessTree()
{
    if (!isRunning()) return;
    const qint64 pid = m_process.processId();

#if defined(Q_OS_WIN)
    // ollama serve ignores WM_CLOSE, so terminate() does nothing. /T also takes
    // down the runner subprocesses that hold the model in memory.
    QProcess killer;
    killer.setProcessChannelMode(QProcess::MergedChannels);
    killer.start(QStringLiteral("taskkill"), QStringList()
                 << QStringLiteral("/PID") << QString::number(pid) << QStringLiteral("/T"));
    killer.waitForFinished(2500);

    if (isRunning()) {
        QProcess forcer;
        forcer.setProcessChannelMode(QProcess::MergedChannels);
        forcer.start(QStringLiteral("taskkill"), QStringList()
                     << QStringLiteral("/PID") << QString::number(pid)
                     << QStringLiteral("/T") << QStringLiteral("/F"));
        forcer.waitForFinished(2500);
        if (forcer.exitCode() != 0) {
            emit logLine(LogLevel::Warn,
                         T("Kill process tree returned %1: %2")
                             .arg(forcer.exitCode())
                             .arg(QString::fromLocal8Bit(forcer.readAll()).trimmed()));
        }
    }
#else
    m_process.terminate();
    if (!m_process.waitForFinished(2000)) m_process.kill();
#endif

    if (isRunning()) m_process.waitForFinished(1500);
}

void OllamaProcess::resetAfterStop()
{
    emitBuffered(true);
    m_stopRequested = false;
    emit stateChanged(false);
}

QString OllamaProcess::commandLine() const
{
    QStringList parts;
    parts << m_program << m_arguments;
    return parts.join(QLatin1Char(' '));
}

void OllamaProcess::onReadyRead()
{
    m_buffer.append(m_process.readAll());
    emitBuffered(false);
}

void OllamaProcess::emitBuffered(bool flushTail)
{
    int nl = m_buffer.indexOf('\n');
    while (nl >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty()) {
            const QString text = QString::fromUtf8(line);
            emit logLine(classify(text), text);
        }
        nl = m_buffer.indexOf('\n');
    }
    if (flushTail && !m_buffer.isEmpty()) {
        const QString text = QString::fromUtf8(m_buffer);
        if (!text.trimmed().isEmpty()) emit logLine(classify(text), text);
        m_buffer.clear();
    }
}

OllamaProcess::LogLevel OllamaProcess::classify(const QString &line)
{
    const QString low = line.toLower();

    // Gin access log, e.g.
    //   [GIN] 2024/05/01 - 12:00:00 | 200 | 713.2µs | 127.0.0.1 | GET "/api/ps"
    // Successful probe requests are noise, so they drop to Debug; 4xx/5xx stay
    // visible as warnings and errors.
    if (low.contains(QStringLiteral("[gin]"))) {
        static const QRegularExpression re(QStringLiteral("\\|\\s*(\\d{3})\\s*\\|"));
        const QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            const int code = match.captured(1).toInt();
            if (code >= 500) return LogLevel::Error;
            if (code >= 400) return LogLevel::Warn;
            return LogLevel::Debug;
        }
        return LogLevel::Debug;
    }

    // Cloud lookups fail on offline machines; harmless for a local server.
    if (low.contains(QStringLiteral("ollama.com"))
        && (low.contains(QStringLiteral("dial tcp"))
            || low.contains(QStringLiteral("no such host"))
            || low.contains(QStringLiteral("hydration failed"))
            || low.contains(QStringLiteral("recommendations refresh failed")))) {
        return LogLevel::Info;
    }

    // Harmless startup chatter from newer builds (UI flag deprecation, listener).
    if (low.contains(QStringLiteral("init_listene"))
        || low.contains(QStringLiteral("--no-ui"))
        || low.contains(QStringLiteral("--webui"))
        || low.contains(QStringLiteral("use --ui"))
        || low.contains(QStringLiteral("deprecated --webui"))) {
        return LogLevel::Info;
    }

    // Structured levels win over keyword heuristics. A JSON response body can
    // contain an "error" field that is not a log level.
    if (low.contains(QStringLiteral("level=error")) || low.contains(QStringLiteral("level=fatal")))
        return LogLevel::Error;
    if (low.contains(QStringLiteral("level=warn"))) return LogLevel::Warn;
    if (low.contains(QStringLiteral("level=debug"))) return LogLevel::Debug;
    if (low.contains(QStringLiteral("level=info"))) return LogLevel::Info;

    const QString trimmed = low.trimmed();
    if (trimmed.startsWith(QStringLiteral("fatal")) || trimmed.startsWith(QStringLiteral("panic")))
        return LogLevel::Error;
    if (trimmed.startsWith(QStringLiteral("warn")) || low.contains(QStringLiteral("deprecat")))
        return LogLevel::Warn;
    if (low.contains(QStringLiteral("error")) || low.contains(QStringLiteral(" failed")))
        return LogLevel::Error;
    return LogLevel::Info;
}

void OllamaProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool crashed = (status != QProcess::NormalExit);

    if (m_stopRequested) {
        // taskkill /F always reports CrashExit; that is expected here.
        emit logLine(LogLevel::Info, T("ollama serve stopped (exit=%1).").arg(exitCode));
        resetAfterStop();
        return;
    }

    emitBuffered(true);
    const LogLevel level = (crashed || exitCode != 0) ? LogLevel::Error : LogLevel::Info;
    emit logLine(level, T("ollama serve exited (exit=%1, status=%2)")
                            .arg(exitCode)
                            .arg(crashed ? QStringLiteral("crash") : QStringLiteral("normal")));
    if (crashed || exitCode != 0) {
        emit processError(
            T("ollama serve exited unexpectedly (exit=%1). If you did not stop it, check the log above.")
                .arg(exitCode));
    }
    resetAfterStop();
}

void OllamaProcess::onErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        const QString msg = T("Cannot start ollama.exe: ") + m_process.errorString();
        emit processError(msg);
        emit logLine(LogLevel::Error, msg);
        emit stateChanged(false);
    } else if (error == QProcess::Crashed && !m_stopRequested) {
        emit logLine(LogLevel::Error,
                     T("ollama.exe terminated abnormally: ") + m_process.errorString());
    }
}
