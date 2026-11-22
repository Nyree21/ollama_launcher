#include <QApplication>
#include <QFile>
#include <QFont>
#include <QMessageBox>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include "app_config.h"
#include "i18n.h"
#include "main_window.h"
#include "ollama_monitor.h"
#include "ollama_process.h"
#include "single_instance.h"
#include "stats_proxy.h"
#include "tray_manager.h"

namespace
{
// Declare per-monitor DPI awareness before QApplication exists. Without it
// Windows bitmap-scales the whole window, which blurs text and clips labels on
// 125%/150% displays.
void enableDpiAwareness()
{
#if defined(Q_OS_WIN)
    using SetDpiAwarenessContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        // The union avoids a FARPROC -> function pointer cast warning.
        union {
            FARPROC raw;
            SetDpiAwarenessContextFn fn;
        } convert;
        convert.raw = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (convert.fn && convert.fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware(); // Vista+ fallback
#endif
}
} // namespace

int main(int argc, char **argv)
{
    enableDpiAwareness();

    // QApplication strips the arguments it recognises, so keep a copy first.
    QStringList rawArgs;
    for (int i = 0; i < argc; ++i) rawArgs << QString::fromLocal8Bit(argv[i]);

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("OllamaLauncher"));
    app.setApplicationName(QStringLiteral("Ollama Launcher"));
#if defined(OL_APP_VERSION)
    app.setApplicationVersion(QStringLiteral(OL_APP_VERSION));
#else
    app.setApplicationVersion(QStringLiteral("1.0.0"));
#endif
    app.setQuitOnLastWindowClosed(false); // tray app

    bool minimized = false;
    bool noProxy = false;
    bool skipTrayCheck = false;
    bool requestQuit = false;
    QString configPath;
    for (int i = 1; i < rawArgs.size(); ++i) {
        const QString &arg = rawArgs.at(i);
        if (arg == QLatin1String("--minimized")) minimized = true;
        else if (arg == QLatin1String("--no-proxy")) noProxy = true;
        else if (arg == QLatin1String("--no-tray-check")) skipTrayCheck = true;
        else if (arg == QLatin1String("--quit")) requestQuit = true;
        else if (arg == QLatin1String("--config") && i + 1 < rawArgs.size()) configPath = rawArgs.at(++i);
    }

    // The guard runs before anything touches the file system, so a second launch
    // (or --quit) never creates or rewrites the config file.
    SingleInstance instance(QStringLiteral("main"));
    if (!instance.isPrimary()) {
        if (requestQuit) instance.sendQuitRequest();
        else instance.sendShowRequest();
        return 0;
    }
    if (requestQuit) return 0; // no running instance

    AppConfig config(configPath.isEmpty() ? AppConfig::defaultPath() : configPath);
    QString configError;
    if (!config.load(&configError)) {
        QMessageBox::warning(nullptr, QStringLiteral("Ollama Launcher"), configError);
    }

    // Language first: every string below is translated at construction time.
    i18n::setLanguage(i18n::fromCode(config.data().language));
    i18n::applyFont();

    QFile styleSheet(QStringLiteral(":/app/style.qss"));
    if (styleSheet.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(styleSheet.readAll()));
    app.setWindowIcon(TrayManager::makeIcon(false));

    if (!skipTrayCheck && !QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::warning(nullptr, QStringLiteral("Ollama Launcher"),
                             T("No system tray detected. The app runs as a normal window."));
    }

    if (noProxy) config.data().proxyEnabled = false;

    OllamaProcess process;
    process.configure(config.data());
    OllamaMonitor monitor(config.data());
    StatsProxy proxy(config.data());

    QSystemTrayIcon tray(TrayManager::makeIcon(false));
    tray.show();

    MainWindow window(&config, &process, &monitor, &proxy, &tray);
    QObject::connect(&instance, &SingleInstance::showRequested, &window, &MainWindow::showWindow);
    QObject::connect(&instance, &SingleInstance::quitRequested, &app, &QApplication::quit);

    if (config.data().proxyEnabled) {
        QString proxyError;
        if (proxy.start(&proxyError)) {
            window.appendSystemLog(
                OllamaProcess::LogLevel::Info,
                T("Stats proxy listening on %1:%2. Point clients here to collect token/TTFT stats.")
                    .arg(config.data().host)
                    .arg(config.data().proxyPort));
        } else {
            window.appendSystemLog(OllamaProcess::LogLevel::Error, proxyError);
        }
    }

    if (config.data().autoStart)
        QTimer::singleShot(400, &window, &MainWindow::startOllama);

    // Stop the child process on exit so no orphan keeps the port or the VRAM.
    QObject::connect(&app, &QApplication::aboutToQuit, [&process]() {
        if (process.isRunning()) process.stopAndWait();
    });

    if (minimized) window.hide();
    else window.show();

    return app.exec();
}
