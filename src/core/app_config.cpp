#include "app_config.h"

#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace
{
// Probe the usual install locations, then fall back to PATH. An empty result
// means the user has to pick the executable in the settings dialog.
QString detectOllama()
{
    QStringList candidates;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        candidates << localAppData + QStringLiteral("/Programs/Ollama/ollama.exe");
    candidates << QStringLiteral("C:/Program Files/Ollama/ollama.exe")
               << QDir::homePath() + QStringLiteral("/AppData/Local/Programs/Ollama/ollama.exe");

    for (const QString &path : candidates) {
        if (QFile::exists(path)) return QDir::toNativeSeparators(path);
    }
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("ollama"));
    return onPath.isEmpty() ? QString() : QDir::toNativeSeparators(onPath);
}

QString detectModelsDir()
{
    return QDir::toNativeSeparators(QDir::homePath() + QStringLiteral("/.ollama/models"));
}
} // namespace

QString LauncherConfig::connectHost() const
{
    if (host.isEmpty() || host == QLatin1String("0.0.0.0") || host == QLatin1String("::"))
        return QStringLiteral("127.0.0.1");
    return host;
}

AppConfig::AppConfig(QString path)
    : m_path(path.isEmpty() ? defaultPath() : std::move(path))
{
}

QString AppConfig::defaultPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ollama_launcher.ini");
}

bool AppConfig::load(QString *error)
{
    QSettings ini(m_path, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    ini.setIniCodec("UTF-8");
#endif

    m_data.configPath = m_path;
    m_data.ollamaPath = ini.value(QStringLiteral("server/ollama_path"), detectOllama()).toString();
    m_data.modelsDir = ini.value(QStringLiteral("server/models_dir"), detectModelsDir()).toString();
    m_data.host = ini.value(QStringLiteral("server/host"), QStringLiteral("127.0.0.1")).toString();
    // Out-of-range values in a hand-edited file would silently wrap around when
    // narrowed to quint16, so fall back to the defaults instead.
    const int port = ini.value(QStringLiteral("server/port"), 11434).toInt();
    m_data.port = static_cast<quint16>((port > 0 && port <= 65535) ? port : 11434);
    m_data.extraArgs = ini.value(QStringLiteral("server/extra_args"), QString()).toString();

    m_data.proxyEnabled = ini.value(QStringLiteral("proxy/enabled"), true).toBool();
    const int proxyPort = ini.value(QStringLiteral("proxy/port"), 11500).toInt();
    m_data.proxyPort = static_cast<quint16>((proxyPort > 0 && proxyPort <= 65535) ? proxyPort : 11500);

    m_data.autoStart = ini.value(QStringLiteral("ui/auto_start"), false).toBool();
    m_data.closeToTray = ini.value(QStringLiteral("ui/close_to_tray"), true).toBool();
    m_data.refreshMs = ini.value(QStringLiteral("ui/refresh_ms"), 2000).toInt();
    m_data.logLevel = ini.value(QStringLiteral("ui/log_level"), QStringLiteral("info")).toString();
    m_data.language = ini.value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();

    m_data.extraEnv.clear();
    ini.beginGroup(QStringLiteral("env"));
    const QStringList envKeys = ini.allKeys();
    for (const QString &key : envKeys)
        m_data.extraEnv.insert(key, ini.value(key).toString());
    ini.endGroup();

    // First run: drop a commented template so the file can be edited by hand.
    if (!QFile::exists(m_path)) save();
    if (error) error->clear();
    return true;
}

void AppConfig::save() const
{
    QSettings ini(m_path, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    ini.setIniCodec("UTF-8");
#endif
    ini.setValue(QStringLiteral("server/ollama_path"), m_data.ollamaPath);
    ini.setValue(QStringLiteral("server/models_dir"), m_data.modelsDir);
    ini.setValue(QStringLiteral("server/host"), m_data.host);
    ini.setValue(QStringLiteral("server/port"), static_cast<int>(m_data.port));
    ini.setValue(QStringLiteral("server/extra_args"), m_data.extraArgs);
    ini.setValue(QStringLiteral("proxy/enabled"), m_data.proxyEnabled);
    ini.setValue(QStringLiteral("proxy/port"), static_cast<int>(m_data.proxyPort));
    ini.setValue(QStringLiteral("ui/auto_start"), m_data.autoStart);
    ini.setValue(QStringLiteral("ui/close_to_tray"), m_data.closeToTray);
    ini.setValue(QStringLiteral("ui/refresh_ms"), m_data.refreshMs);
    ini.setValue(QStringLiteral("ui/log_level"), m_data.logLevel);
    ini.setValue(QStringLiteral("ui/language"), m_data.language);
    ini.sync();

    ini.beginGroup(QStringLiteral("env"));
    for (auto it = m_data.extraEnv.constBegin(); it != m_data.extraEnv.constEnd(); ++it)
        ini.setValue(it.key(), it.value());
    ini.endGroup();
    ini.sync();
}

bool AppConfig::validate(QString *error) const
{
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };

    if (m_data.ollamaPath.trimmed().isEmpty())
        return fail(T("No ollama.exe path configured. Set it in Settings."));

    if (!m_data.ollamaPath.contains(QLatin1Char('/')) && !m_data.ollamaPath.contains(QLatin1Char('\\'))) {
        if (QStandardPaths::findExecutable(m_data.ollamaPath).isEmpty())
            return fail(T("ollama.exe was not found in PATH."));
    } else if (!QFile::exists(m_data.ollamaPath)) {
        return fail(T("ollama.exe does not exist: ") + m_data.ollamaPath);
    }

    if (m_data.host.trimmed().isEmpty()) return fail(T("The listen address cannot be empty."));
    if (m_data.port == 0) return fail(T("The port must be between 1 and 65535."));
    if (m_data.proxyEnabled && m_data.proxyPort == 0)
        return fail(T("The proxy port must be between 1 and 65535."));
    if (m_data.proxyEnabled && m_data.proxyPort == m_data.port)
        return fail(T("The proxy port must differ from the Ollama port."));
    if (m_data.refreshMs < 500) return fail(T("The refresh interval must be at least 500 ms."));

    if (error) error->clear();
    return true;
}
