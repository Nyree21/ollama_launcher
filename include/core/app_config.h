// Runtime configuration loaded from ollama_launcher.ini next to the executable.
// Nothing is written to the registry and no system settings are touched.
#ifndef OL_CORE_APP_CONFIG_H
#define OL_CORE_APP_CONFIG_H

#include <QMap>
#include <QString>

struct LauncherConfig
{
    QString ollamaPath;   // absolute path of ollama.exe
    QString modelsDir;    // passed to the child process as OLLAMA_MODELS
    QString host;         // listen address
    quint16 port = 11434;
    QString extraArgs;    // appended to "ollama serve"

    QMap<QString, QString> extraEnv; // extra environment variables (KEY -> VALUE)

    bool proxyEnabled = true;  // local stats proxy
    quint16 proxyPort = 11500;

    bool autoStart = false;   // start ollama when the launcher starts
    bool closeToTray = true;  // closing the window only hides it
    int refreshMs = 2000;     // monitor polling interval
    QString logLevel = QStringLiteral("info");
    QString language = QStringLiteral("en");

    QString configPath; // ini file actually in use

    // Address used for outgoing requests. Listen addresses such as 0.0.0.0 or ::
    // map to loopback, because connecting to the wildcard address is not
    // portable.
    QString connectHost() const;
};

class AppConfig
{
public:
    explicit AppConfig(QString path = QString());

    static QString defaultPath();

    bool load(QString *error = nullptr);
    void save() const;

    // Checks paths and ports; on failure `error` receives a translated reason.
    bool validate(QString *error = nullptr) const;

    const LauncherConfig &data() const { return m_data; }
    LauncherConfig &data() { return m_data; }

private:
    LauncherConfig m_data;
    QString m_path;
};

#endif // OL_CORE_APP_CONFIG_H
