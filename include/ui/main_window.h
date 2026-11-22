// Main window: service control, live metrics, logs, connected clients and the
// entry point to the settings dialog.
#ifndef OL_UI_MAIN_WINDOW_H
#define OL_UI_MAIN_WINDOW_H

#include <QDateTime>
#include <QMainWindow>
#include <QVector>

#include "ollama_monitor.h"
#include "ollama_process.h"
#include "stats_proxy.h"

class AppConfig;
class LogModel;
class TrayManager;

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QResizeEvent;
class QSystemTrayIcon;
class QTableWidget;
class QTabWidget;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(AppConfig *cfg, OllamaProcess *proc, OllamaMonitor *monitor,
               StatsProxy *proxy, QSystemTrayIcon *tray, QWidget *parent = nullptr);

    // Shows, restores and focuses the window (second launch of the app).
    void showWindow();

    void startOllama();
    void stopOllama();

    void appendSystemLog(OllamaProcess::LogLevel level, const QString &text);

public slots:
    void toggleWindow();

private slots:
    void onProcessState(bool running);
    void onProcessLog(OllamaProcess::LogLevel level, const QString &text);
    void onProcessError(const QString &message);
    void onSnapshot(const MonitorSnapshot &snapshot);
    void onStats(const ProxyStats &stats);
    void onProbeFinished(double ttftMs, qint64 tokens, double tokensPerSec, const QString &error);
    void onRefreshTimer();
    void onOpenSettings();
    void onOpenConfigFile();
    void onExportLog();
    void onLogFilterChanged();
    void onCopySelectedLog();
    void onCopyAllLogs();
    void onResetStats();
    void onProbeClick();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Every fixed UI string is registered once and re-applied by retranslateUi().
    enum class TextKind { Text, Tooltip, Placeholder };
    struct TextItem {
        QWidget *widget = nullptr;
        QString english;
        TextKind kind = TextKind::Text;
    };

    QFrame *createCard(const QString &title);
    QFrame *createMetric(const QString &caption, QLabel **valueOut);
    QWidget *buildHeader();
    QWidget *buildControl();
    QWidget *buildMetrics();
    QWidget *buildLogTab();
    QWidget *buildClientsTab();
    QWidget *buildModelsTab();
    QWidget *buildSettingsTab();

    void registerText(QWidget *widget, const QString &english,
                      TextKind kind = TextKind::Text);
    void retranslateUi();

    void updateClientTable(const QVector<TcpClient> &clients);
    void updateModelTable(const QVector<RunningModel> &models);
    void updateStatusPill(bool running, const QString &text);
    void updateAddressLabels();
    void refreshStatusText();
    void updateFollowButton();
    void rebuildLogLevelCombo();
    void updateTrayTooltip();
    void applyConfigAfterSave();
    void updateMetricLayout();
    void resetProbeUi();
    void refreshMetricsState();

    AppConfig *m_cfg = nullptr;
    OllamaProcess *m_proc = nullptr;
    OllamaMonitor *m_monitor = nullptr;
    StatsProxy *m_proxy = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    TrayManager *m_trayManager = nullptr;

    QVector<TextItem> m_texts;

    QTabWidget *m_tabs = nullptr;

    LogModel *m_logModel = nullptr;
    QListView *m_logView = nullptr;
    QComboBox *m_logLevelCombo = nullptr;
    QLineEdit *m_logKeyword = nullptr;
    QCheckBox *m_hideAccessCheck = nullptr;
    QPushButton *m_followBtn = nullptr;
    QAction *m_copySelectedAction = nullptr;
    QAction *m_copyAllAction = nullptr;
    bool m_followLatest = true;
    bool m_internalScroll = false; // scrolling done by the app, not the user

    QLabel *m_statusPill = nullptr;
    QLabel *m_uptimeLabel = nullptr;
    QLabel *m_lastUpdateLabel = nullptr;
    QLabel *m_serviceAddrLabel = nullptr;
    QLabel *m_proxyAddrLabel = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;

    // Metric cards are re-laid out when the window width crosses a threshold.
    struct MetricCard {
        QFrame *frame = nullptr;
        QLabel *caption = nullptr;
        QLabel *value = nullptr;
    };
    QVector<MetricCard> m_metricCards;
    QGridLayout *m_metricsGrid = nullptr;
    int m_metricCols = 0; // 0 = not laid out yet, forces the first pass

    QLabel *m_lbRunningModels = nullptr;
    QLabel *m_lbInstalledModels = nullptr;
    QLabel *m_lbProcessMemory = nullptr;
    QLabel *m_lbModelVram = nullptr;
    QLabel *m_lbTokens = nullptr;
    QLabel *m_lbTtft = nullptr;
    QLabel *m_lbTps = nullptr;
    QLabel *m_lbClients = nullptr;
    QLabel *m_lbErrors = nullptr;

    QComboBox *m_probeModelCombo = nullptr;
    QPushButton *m_probeBtn = nullptr;
    bool m_probeLockLabels = false; // keep probe results while stats keep streaming

    QTableWidget *m_clientTable = nullptr;
    QTableWidget *m_modelTable = nullptr;

    // Signatures of the last rendered rows; repainting identical tables would
    // drop the user's selection every poll.
    QString m_clientSignature;
    QString m_modelSignature;

    QTimer *m_refreshTimer = nullptr;
    QDateTime m_startedAt;
    MonitorSnapshot m_lastSnapshot;
    ProxyStats m_lastStats;
    bool m_trayHintShown = false;
};

#endif // OL_UI_MAIN_WINDOW_H
