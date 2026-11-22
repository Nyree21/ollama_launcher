#include "main_window.h"

#include "app_config.h"
#include "i18n.h"
#include "log_model.h"
#include "settings_dialog.h"
#include "tray_manager.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(AppConfig *cfg, OllamaProcess *proc, OllamaMonitor *monitor,
                       StatsProxy *proxy, QSystemTrayIcon *tray, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg), m_proc(proc), m_monitor(monitor),
      m_proxy(proxy), m_tray(tray)
{
    setMinimumSize(QSize(1050, 720));
    resize(QSize(1180, 860));
    setWindowIcon(TrayManager::makeIcon(false));

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralwidget"));
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());

    // Control + metrics live in a scroll area: when the window gets short the
    // content scrolls instead of squeezing the cards into each other.
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("content"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 14, 18, 14);
    contentLayout->setSpacing(14);
    contentLayout->addWidget(buildControl());
    contentLayout->addWidget(buildMetrics());
    contentLayout->addStretch(1);

    auto *topScroll = new QScrollArea(this);
    topScroll->setWidgetResizable(true);
    topScroll->setFrameShape(QFrame::NoFrame);
    topScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    topScroll->setWidget(content);
    topScroll->setMinimumHeight(240);
    topScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    root->addWidget(topScroll, 2);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildLogTab(), QString());
    m_tabs->addTab(buildClientsTab(), QString());
    m_tabs->addTab(buildModelsTab(), QString());
    m_tabs->addTab(buildSettingsTab(), QString());
    m_tabs->setMinimumHeight(200);
    root->addWidget(m_tabs, 1);

    m_lastUpdateLabel = new QLabel(this);
    statusBar()->addWidget(m_lastUpdateLabel);

    m_trayManager = new TrayManager(tray, this);
    connect(m_trayManager, &TrayManager::showRequested, this, &MainWindow::toggleWindow);
    connect(m_trayManager, &TrayManager::startRequested, this, &MainWindow::startOllama);
    connect(m_trayManager, &TrayManager::stopRequested, this, &MainWindow::stopOllama);
    connect(m_trayManager, &TrayManager::quitRequested, qApp, &QApplication::quit);

    connect(m_proc, &OllamaProcess::stateChanged, this, &MainWindow::onProcessState);
    connect(m_proc, &OllamaProcess::logLine, this, &MainWindow::onProcessLog);
    connect(m_proc, &OllamaProcess::processError, this, &MainWindow::onProcessError);
    connect(m_monitor, &OllamaMonitor::snapshotReady, this, &MainWindow::onSnapshot);
    connect(m_monitor, &OllamaMonitor::probeFinished, this, &MainWindow::onProbeFinished);
    connect(m_proxy, &StatsProxy::statsChanged, this, &MainWindow::onStats);
    connect(m_proxy, &StatsProxy::logMessage, this, &MainWindow::onProcessLog);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(m_cfg->data().refreshMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    m_refreshTimer->start();

    retranslateUi();
    onLogFilterChanged();
    onProcessState(m_proc->isRunning());
    onStats(m_proxy->stats());
    onRefreshTimer();
    updateMetricLayout();

    m_logModel->append(OllamaProcess::LogLevel::Info,
                       T("Launcher ready. Config file: %1").arg(m_cfg->data().configPath));
    statusBar()->showMessage(T("Ready"));
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MainWindow::registerText(QWidget *widget, const QString &english, TextKind kind)
{
    if (!widget || english.isEmpty()) return;
    m_texts.append(TextItem{widget, english, kind});
}

QFrame *MainWindow::createCard(const QString &title)
{
    auto *frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("card"));

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto *label = new QLabel(frame);
    label->setObjectName(QStringLiteral("cardTitle"));
    registerText(label, title);
    layout->addWidget(label);
    return frame;
}

QFrame *MainWindow::createMetric(const QString &caption, QLabel **valueOut)
{
    auto *frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("card"));
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    frame->setMinimumHeight(70);

    auto *captionLabel = new QLabel(frame);
    captionLabel->setObjectName(QStringLiteral("metricCaption"));
    captionLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    registerText(captionLabel, caption);

    auto *value = new QLabel(QStringLiteral("—"), frame);
    value->setObjectName(QStringLiteral("metricValue"));
    value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    value->setMinimumHeight(34);

    m_metricCards.append(MetricCard{frame, captionLabel, value});
    *valueOut = value;
    return frame;
}

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("header"));

    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(22, 14, 22, 14);

    auto *texts = new QVBoxLayout();
    auto *title = new QLabel(header);
    title->setObjectName(QStringLiteral("title"));
    registerText(title, QStringLiteral("Ollama Launcher"));
    auto *subtitle = new QLabel(header);
    subtitle->setObjectName(QStringLiteral("subtitle"));
    registerText(subtitle, QStringLiteral("Start, stop and monitor your local Ollama service"));
    texts->addWidget(title);
    texts->addWidget(subtitle);
    layout->addLayout(texts);
    layout->addStretch(1);

    m_uptimeLabel = new QLabel(header);
    m_uptimeLabel->setObjectName(QStringLiteral("subtitle"));
    m_uptimeLabel->setMinimumWidth(110); // keep digits visible on scaled displays
    m_uptimeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    layout->addWidget(m_uptimeLabel);

    m_statusPill = new QLabel(header);
    m_statusPill->setObjectName(QStringLiteral("statusPill"));
    m_statusPill->setAlignment(Qt::AlignCenter);
    m_statusPill->setMinimumWidth(72);
    m_statusPill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    layout->addWidget(m_statusPill);
    return header;
}

QWidget *MainWindow::buildControl()
{
    QFrame *card = createCard(QStringLiteral("Service control"));
    auto *layout = qobject_cast<QVBoxLayout *>(card->layout());

    auto *buttons = new QHBoxLayout();
    m_startBtn = new QPushButton(card);
    m_startBtn->setObjectName(QStringLiteral("primaryBtn"));
    registerText(m_startBtn, QStringLiteral("▶  Start Ollama"));
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::startOllama);

    m_stopBtn = new QPushButton(card);
    m_stopBtn->setObjectName(QStringLiteral("dangerBtn"));
    registerText(m_stopBtn, QStringLiteral("■  Stop Ollama"));
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::stopOllama);

    buttons->addWidget(m_startBtn);
    buttons->addWidget(m_stopBtn);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *serviceRow = new QHBoxLayout();
    m_serviceAddrLabel = new QLabel(card);
    m_serviceAddrLabel->setObjectName(QStringLiteral("cardHint"));
    auto *copyService = new QPushButton(card);
    registerText(copyService, QStringLiteral("Copy service URL"));
    connect(copyService, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_serviceAddrLabel->text());
    });
    serviceRow->addWidget(m_serviceAddrLabel);
    serviceRow->addStretch(1);
    serviceRow->addWidget(copyService);
    layout->addLayout(serviceRow);

    auto *proxyRow = new QHBoxLayout();
    m_proxyAddrLabel = new QLabel(card);
    m_proxyAddrLabel->setObjectName(QStringLiteral("cardHint"));
    auto *copyProxy = new QPushButton(card);
    registerText(copyProxy, QStringLiteral("Copy client URL"));
    connect(copyProxy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_proxyAddrLabel->text());
    });
    proxyRow->addWidget(m_proxyAddrLabel);
    proxyRow->addStretch(1);
    proxyRow->addWidget(copyProxy);
    layout->addLayout(proxyRow);

    auto *hint = new QLabel(card);
    hint->setObjectName(QStringLiteral("cardHint"));
    hint->setWordWrap(true);
    registerText(hint, QStringLiteral(
        "Tip: point your client's base URL at the client URL to collect real token and TTFT "
        "statistics. Without the proxy, clients talk to Ollama directly."));
    layout->addWidget(hint);

    // The benchmark lives here so it is reachable from the main view.
    auto *probeRow = new QHBoxLayout();
    auto *probeCaption = new QLabel(card);
    registerText(probeCaption, QStringLiteral("Model benchmark"));
    m_probeModelCombo = new QComboBox(card);
    m_probeModelCombo->setMinimumWidth(220);
    registerText(m_probeModelCombo,
                 QStringLiteral("Pick a locally installed model to measure TTFT"),
                 TextKind::Tooltip);
    m_probeBtn = new QPushButton(card);
    m_probeBtn->setObjectName(QStringLiteral("secondaryBtn"));
    registerText(m_probeBtn, QStringLiteral("Benchmark now (TTFT)"));
    connect(m_probeBtn, &QPushButton::clicked, this, &MainWindow::onProbeClick);

    probeRow->addWidget(probeCaption);
    probeRow->addWidget(m_probeModelCombo, 1);
    probeRow->addWidget(m_probeBtn);
    probeRow->addStretch(1);
    layout->addLayout(probeRow);

    return card;
}

QWidget *MainWindow::buildMetrics()
{
    auto *wrapper = new QWidget(this);
    m_metricsGrid = new QGridLayout(wrapper);
    m_metricsGrid->setContentsMargins(0, 0, 0, 0);
    m_metricsGrid->setSpacing(12);

    int row = 0;
    int column = 0;
    auto add = [&](const QString &caption, QLabel **out) {
        m_metricsGrid->addWidget(createMetric(caption, out), row, column);
        if (++column == 4) {
            column = 0;
            ++row;
        }
    };

    add(QStringLiteral("Running models"), &m_lbRunningModels);
    add(QStringLiteral("Installed models"), &m_lbInstalledModels);
    add(QStringLiteral("Process memory / private (MB)"), &m_lbProcessMemory);
    add(QStringLiteral("Model VRAM (MB)"), &m_lbModelVram);
    add(QStringLiteral("Generated tokens"), &m_lbTokens);
    add(QStringLiteral("First token latency (ms)"), &m_lbTtft);
    add(QStringLiteral("Generation speed (tok/s)"), &m_lbTps);
    add(QStringLiteral("Client connections"), &m_lbClients);
    add(QStringLiteral("Errors / warnings"), &m_lbErrors);
    return wrapper;
}

QWidget *MainWindow::buildLogTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_logModel = new LogModel(this);

    auto *toolbar = new QHBoxLayout();
    m_logLevelCombo = new QComboBox(page);
    m_logLevelCombo->addItem(QString(), static_cast<int>(OllamaProcess::LogLevel::Debug));
    m_logLevelCombo->addItem(QString(), static_cast<int>(OllamaProcess::LogLevel::Info));
    m_logLevelCombo->addItem(QString(), static_cast<int>(OllamaProcess::LogLevel::Warn));
    m_logLevelCombo->addItem(QString(), static_cast<int>(OllamaProcess::LogLevel::Error));
    m_logLevelCombo->setCurrentIndex(1); // "info and above" hides the probe noise

    m_logKeyword = new QLineEdit(page);
    registerText(m_logKeyword,
                 QStringLiteral("Search (model name, error code, request path...)"),
                 TextKind::Placeholder);

    m_hideAccessCheck = new QCheckBox(page);
    registerText(m_hideAccessCheck, QStringLiteral("Hide access logs"));
    m_hideAccessCheck->setChecked(true);
    registerText(m_hideAccessCheck,
                 QStringLiteral("Hides [GIN] ... GET /api/* monitor probe requests"),
                 TextKind::Tooltip);

    m_followBtn = new QPushButton(page);
    m_followBtn->setObjectName(QStringLiteral("followBtn"));
    m_followBtn->setCheckable(true);
    m_followBtn->setChecked(true);
    registerText(m_followBtn,
                 QStringLiteral("Keeps the view at the newest line. Scrolling up unlocks it; "
                                "click to lock again and jump to the end."),
                 TextKind::Tooltip);

    auto *resetBtn = new QPushButton(page);
    registerText(resetBtn, QStringLiteral("Reset stats"));
    auto *clearBtn = new QPushButton(page);
    registerText(clearBtn, QStringLiteral("Clear logs"));
    auto *exportBtn = new QPushButton(page);
    registerText(exportBtn, QStringLiteral("Export logs"));

    toolbar->addWidget(m_logLevelCombo);
    toolbar->addWidget(m_logKeyword, 1);
    toolbar->addWidget(m_hideAccessCheck);
    toolbar->addWidget(m_followBtn);
    toolbar->addWidget(resetBtn);
    toolbar->addWidget(clearBtn);
    toolbar->addWidget(exportBtn);
    layout->addLayout(toolbar);

    m_logView = new QListView(page);
    m_logView->setObjectName(QStringLiteral("logView"));
    m_logView->setModel(m_logModel);
    m_logView->setAlternatingRowColors(false); // would fight the red error rows
    m_logView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_logView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logView->setFocusPolicy(Qt::StrongFocus);
    m_logView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logView->setContextMenuPolicy(Qt::ActionsContextMenu);

    m_copySelectedAction = new QAction(m_logView);
    m_copySelectedAction->setShortcut(QKeySequence::Copy);
    m_copyAllAction = new QAction(m_logView);
    m_logView->addAction(m_copySelectedAction);
    m_logView->addAction(m_copyAllAction);
    connect(m_copySelectedAction, &QAction::triggered, this, &MainWindow::onCopySelectedLog);
    connect(m_copyAllAction, &QAction::triggered, this, &MainWindow::onCopyAllLogs);

    // Auto-scroll while "follow" is locked; scrolling up unlocks it.
    auto scrollToLatest = [this]() {
        m_internalScroll = true;
        m_logView->scrollToBottom();
        QTimer::singleShot(0, this, [this]() { m_internalScroll = false; });
    };
    connect(m_logModel, &QAbstractItemModel::rowsInserted, this, [this, scrollToLatest]() {
        if (m_followLatest) scrollToLatest();
    });
    connect(m_logModel, &QAbstractItemModel::modelReset, this, [this, scrollToLatest]() {
        if (m_followLatest) scrollToLatest();
    });
    connect(m_logView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_internalScroll || !m_followLatest) return;
        QScrollBar *bar = m_logView->verticalScrollBar();
        if (bar && value < bar->maximum() - 2) {
            m_followLatest = false;
            m_followBtn->setChecked(false); // triggers toggled -> label refresh
        }
    });
    connect(m_followBtn, &QPushButton::toggled, this, [this, scrollToLatest](bool checked) {
        m_followLatest = checked;
        updateFollowButton();
        if (checked) QTimer::singleShot(0, this, [scrollToLatest]() { scrollToLatest(); });
    });

    layout->addWidget(m_logView, 1);

    connect(m_logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLogFilterChanged);
    connect(m_logKeyword, &QLineEdit::textChanged, this, &MainWindow::onLogFilterChanged);
    connect(m_hideAccessCheck, &QCheckBox::toggled, this, &MainWindow::onLogFilterChanged);
    connect(clearBtn, &QPushButton::clicked, m_logModel, &LogModel::clear);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::onExportLog);
    connect(resetBtn, &QPushButton::clicked, this, &MainWindow::onResetStats);
    return page;
}

QWidget *MainWindow::buildClientsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);

    m_clientTable = new QTableWidget(0, 5, page);
    m_clientTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_clientTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_clientTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_clientTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_clientTable->setAlternatingRowColors(true);
    layout->addWidget(m_clientTable);

    auto *hint = new QLabel(page);
    hint->setObjectName(QStringLiteral("cardHint"));
    registerText(hint, QStringLiteral(
        "Established connections are enumerated with the Win32 GetExtendedTcpTable API and "
        "resolve to a process name; the list refreshes on every poll."));
    layout->addWidget(hint);
    return page;
}

QWidget *MainWindow::buildModelsTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);

    m_modelTable = new QTableWidget(0, 4, page);
    m_modelTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_modelTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_modelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_modelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_modelTable->setAlternatingRowColors(true);
    layout->addWidget(m_modelTable);
    return page;
}

QWidget *MainWindow::buildSettingsTab()
{
    // The tab only holds entry points; the form itself is a separate dialog.
    // A scroll area keeps the buttons from overlapping on small heights.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *card = createCard(QStringLiteral("Configuration"));
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *cardLayout = qobject_cast<QVBoxLayout *>(card->layout());
    cardLayout->setContentsMargins(20, 16, 20, 16);
    cardLayout->setSpacing(12);

    auto *description = new QLabel(card);
    description->setObjectName(QStringLiteral("cardHint"));
    description->setWordWrap(true);
    registerText(description, QStringLiteral(
        "All runtime options are edited in the settings dialog. Port and path changes need a "
        "service restart; proxy changes need an app restart."));
    cardLayout->addWidget(description);

    auto *openBtn = new QPushButton(card);
    openBtn->setObjectName(QStringLiteral("primaryBtn"));
    openBtn->setMinimumWidth(180);
    openBtn->setMinimumHeight(40);
    openBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    registerText(openBtn, QStringLiteral("⚙  Open settings"));
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    cardLayout->addWidget(openBtn, 0, Qt::AlignLeft);

    auto *configBtn = new QPushButton(card);
    configBtn->setObjectName(QStringLiteral("secondaryBtn"));
    configBtn->setMinimumHeight(36);
    configBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    registerText(configBtn, QStringLiteral("View config file (advanced)"));
    connect(configBtn, &QPushButton::clicked, this, &MainWindow::onOpenConfigFile);
    cardLayout->addWidget(configBtn, 0, Qt::AlignLeft);

    auto *info = new QLabel(card);
    info->setObjectName(QStringLiteral("cardHint"));
    info->setWordWrap(true);
    registerText(info, QStringLiteral(
        "The config lives next to the executable in ollama_launcher.ini. Nothing is written "
        "to the registry."));
    cardLayout->addWidget(info);

    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch(1);

    scroll->setWidget(content);
    return scroll;
}

// ---------------------------------------------------------------------------
// Language and labels
// ---------------------------------------------------------------------------

void MainWindow::retranslateUi()
{
    setWindowTitle(T("Ollama Launcher"));

    for (const TextItem &item : m_texts) {
        switch (item.kind) {
        case TextKind::Text:
            if (auto *label = qobject_cast<QLabel *>(item.widget))
                label->setText(T(item.english));
            else if (auto *button = qobject_cast<QAbstractButton *>(item.widget))
                button->setText(T(item.english));
            break;
        case TextKind::Tooltip:
            item.widget->setToolTip(T(item.english));
            break;
        case TextKind::Placeholder:
            if (auto *edit = qobject_cast<QLineEdit *>(item.widget))
                edit->setPlaceholderText(T(item.english));
            else if (auto *plain = qobject_cast<QPlainTextEdit *>(item.widget))
                plain->setPlaceholderText(T(item.english));
            break;
        }
    }

    if (m_tabs) {
        m_tabs->setTabText(0, T("Logs"));
        m_tabs->setTabText(1, T("Clients"));
        m_tabs->setTabText(2, T("Running models"));
        m_tabs->setTabText(3, T("Settings"));
    }

    rebuildLogLevelCombo();
    updateFollowButton();

    // The placeholder entry is not touched by onSnapshot(), so update it here.
    if (m_probeModelCombo && m_probeModelCombo->count() > 0)
        m_probeModelCombo->setItemText(0, T("(select a model)"));

    if (m_copySelectedAction) m_copySelectedAction->setText(T("Copy selected log lines"));
    if (m_copyAllAction) m_copyAllAction->setText(T("Copy all logs"));

    if (m_clientTable) {
        m_clientTable->setHorizontalHeaderLabels(
            {QStringLiteral("PID"), T("Process"), T("Executable path"),
             T("Remote address"), T("Remote port")});
    }
    if (m_modelTable) {
        m_modelTable->setHorizontalHeaderLabels(
            {T("Model"), T("Size (MB)"), T("VRAM (MB)"), T("Unload at")});
    }

    updateAddressLabels();
    refreshStatusText();
    refreshMetricsState();
    updateTrayTooltip();
    if (m_trayManager) m_trayManager->retranslateUi();
}

void MainWindow::rebuildLogLevelCombo()
{
    if (!m_logLevelCombo) return;

    const int index = m_logLevelCombo->currentIndex();
    const QSignalBlocker blocker(m_logLevelCombo);
    m_logLevelCombo->clear();
    m_logLevelCombo->addItem(T("All levels"), static_cast<int>(OllamaProcess::LogLevel::Debug));
    m_logLevelCombo->addItem(T("Info and above"), static_cast<int>(OllamaProcess::LogLevel::Info));
    m_logLevelCombo->addItem(T("Warnings and above"), static_cast<int>(OllamaProcess::LogLevel::Warn));
    m_logLevelCombo->addItem(T("Errors only"), static_cast<int>(OllamaProcess::LogLevel::Error));
    m_logLevelCombo->setCurrentIndex(index >= 0 ? index : 1);
}

void MainWindow::updateFollowButton()
{
    if (!m_followBtn) return;
    m_followBtn->setText(m_followLatest ? T("Follow: locked") : T("Follow: unlocked"));
}

void MainWindow::updateAddressLabels()
{
    const LauncherConfig &cfg = m_cfg->data();
    m_serviceAddrLabel->setText(T("Service URL: http://%1:%2").arg(cfg.host).arg(cfg.port));
    m_proxyAddrLabel->setText(
        cfg.proxyEnabled
            ? T("Client URL (stats proxy): http://%1:%2").arg(cfg.host).arg(cfg.proxyPort)
            : T("Client URL: the stats proxy is disabled, connect to the service URL"));
}

void MainWindow::refreshStatusText()
{
    const bool running = m_proc->isRunning();
    updateStatusPill(running, running ? T("Running") : T("Stopped"));

    if (!running || !m_startedAt.isValid()) {
        m_uptimeLabel->setText(T("Not running"));
        return;
    }
    const qint64 seconds = m_startedAt.secsTo(QDateTime::currentDateTime());
    m_uptimeLabel->setText(T("Uptime %1m %2s").arg(seconds / 60).arg(seconds % 60));
}

void MainWindow::refreshMetricsState()
{
    m_lbErrors->setText(QStringLiteral("%1 / %2")
                            .arg(m_logModel->errorCount())
                            .arg(m_logModel->warnCount()));
}

// ---------------------------------------------------------------------------
// Metric card layout
// ---------------------------------------------------------------------------

void MainWindow::updateMetricLayout()
{
    if (!m_metricsGrid || m_metricCards.isEmpty()) return;

    // Narrow window: three columns with "caption + value" on one line.
    // Wide window: four columns, two lines per card.
    const bool compact = width() < 1180;
    const int columns = compact ? 3 : 4;
    // m_metricCols doubles as an "already laid out" flag; 0 forces the first
    // pass, otherwise the cards would never get an internal layout.
    if (columns == m_metricCols) return;
    m_metricCols = columns;

    QVector<QFrame *> frames;
    frames.reserve(m_metricCards.size());
    for (const MetricCard &card : m_metricCards) frames.append(card.frame);
    for (QFrame *frame : frames) m_metricsGrid->removeWidget(frame);

    for (int i = 0; i < m_metricCards.size(); ++i) {
        MetricCard &card = m_metricCards[i];
        if (QLayout *old = card.frame->layout()) {
            old->removeWidget(card.caption);
            old->removeWidget(card.value);
            delete old;
        }

        card.caption->setWordWrap(false);
        card.value->setWordWrap(false);
        card.caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        card.value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        if (compact) {
            auto *row = new QHBoxLayout(card.frame);
            row->setContentsMargins(14, 10, 14, 10);
            row->setSpacing(8);
            card.caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            card.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row->addWidget(card.caption, 1);
            row->addWidget(card.value, 0);
            card.frame->setMinimumHeight(50);
            card.frame->setMaximumHeight(58);
        } else {
            auto *column = new QVBoxLayout(card.frame);
            column->setContentsMargins(14, 12, 14, 12);
            column->setSpacing(4);
            card.caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            card.value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            column->addWidget(card.caption);
            column->addWidget(card.value);
            card.frame->setMinimumHeight(70);
            card.frame->setMaximumHeight(78);
        }
        m_metricsGrid->addWidget(card.frame, i / columns, i % columns);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateMetricLayout();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::showWindow()
{
    setVisible(true);
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void MainWindow::toggleWindow()
{
    if (isVisible() && !isMinimized()) hide();
    else showWindow();
}

void MainWindow::appendSystemLog(OllamaProcess::LogLevel level, const QString &text)
{
    onProcessLog(level, text);
}

void MainWindow::startOllama()
{
    QString error;
    if (!m_cfg->validate(&error)) {
        m_logModel->append(OllamaProcess::LogLevel::Error, error);
        m_tray->showMessage(QStringLiteral("Ollama Launcher"), error,
                            QSystemTrayIcon::Critical, 4000);
        return;
    }

    m_proc->configure(m_cfg->data());
    if (!m_proc->start(&error)) {
        m_logModel->append(OllamaProcess::LogLevel::Error, error);
        m_tray->showMessage(QStringLiteral("Ollama Launcher"), error,
                            QSystemTrayIcon::Critical, 4000);
        return;
    }
    statusBar()->showMessage(T("ollama serve started"));
}

void MainWindow::stopOllama()
{
    // Abort the probe first: otherwise it stays pending until the 30 s timeout
    // and the UI keeps showing "Benchmarking...".
    m_monitor->abortProbe();
    resetProbeUi();
    m_proc->stop();
}

void MainWindow::resetProbeUi()
{
    m_probeLockLabels = false;
    if (m_probeBtn) {
        m_probeBtn->setText(T("Benchmark now (TTFT)"));
        m_probeBtn->setEnabled(true);
    }
    if (m_lbTtft && m_lbTtft->text() == T("Benchmarking...")) m_lbTtft->setText(QStringLiteral("—"));
    if (m_lbTps && m_lbTps->text() == T("Benchmarking...")) m_lbTps->setText(QStringLiteral("—"));
    if (statusBar()->currentMessage().startsWith(T("Benchmarking")))
        statusBar()->showMessage(T("Benchmark cancelled (Ollama stopped)"), 4000);
}

void MainWindow::onProcessState(bool running)
{
    if (!running) {
        // Any stop path (button, tray, crash) must clear the probe state.
        m_monitor->abortProbe();
        resetProbeUi();
    }

    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_trayManager->setRunning(running);
    setWindowIcon(TrayManager::makeIcon(running));
    m_monitor->setProcessPid(m_proc->pid());

    if (running) m_startedAt = QDateTime::currentDateTime();
    else m_startedAt = QDateTime();

    refreshStatusText();
    updateTrayTooltip();
}

void MainWindow::onProcessLog(OllamaProcess::LogLevel level, const QString &text)
{
    m_logModel->append(level, text);
    if (level == OllamaProcess::LogLevel::Error || level == OllamaProcess::LogLevel::Warn)
        refreshMetricsState();
}

void MainWindow::onProcessError(const QString &message)
{
    m_tray->showMessage(T("Ollama service error"), message, QSystemTrayIcon::Warning, 5000);
}

void MainWindow::onSnapshot(const MonitorSnapshot &snapshot)
{
    const bool errorChanged = (snapshot.error != m_lastSnapshot.error);
    m_lastSnapshot = snapshot;

    m_lbRunningModels->setText(QString::number(snapshot.runningModels));
    m_lbInstalledModels->setText(QString::number(snapshot.installedModels));
    m_lbProcessMemory->setText(QStringLiteral("%1 / %2")
                                   .arg(snapshot.processWorkingSetMb, 0, 'f', 1)
                                   .arg(snapshot.processPrivateMb, 0, 'f', 1));
    m_lbModelVram->setText(QString::number(snapshot.modelVramMb, 'f', 1));
    m_lbClients->setText(QString::number(snapshot.clients.size()));

    const QString time = snapshot.updatedAt.toString(QStringLiteral("HH:mm:ss"));
    if (!snapshot.error.isEmpty()) {
        m_lastUpdateLabel->setText(T("Last poll: %1 · %2").arg(time, snapshot.error));
        if (errorChanged && m_proc->isRunning()) {
            m_logModel->append(OllamaProcess::LogLevel::Warn,
                               T("Monitor poll failed: ") + snapshot.error);
        }
    } else {
        m_lastUpdateLabel->setText(
            T("Last poll: %1 · Ollama %2")
                .arg(time, snapshot.version.isEmpty() ? QStringLiteral("—") : snapshot.version));
    }

    // Keep the current selection while refreshing the model list. The list is
    // compared item by item: swapping one model for another keeps the count.
    if (m_probeModelCombo) {
        const QString previous = m_probeModelCombo->currentText();
        bool sameModels = (m_probeModelCombo->count() == snapshot.installedNames.size() + 1);
        for (int i = 0; sameModels && i < snapshot.installedNames.size(); ++i) {
            if (m_probeModelCombo->itemData(i + 1).toString() != snapshot.installedNames.at(i))
                sameModels = false;
        }
        if (!sameModels) {
            m_probeModelCombo->blockSignals(true);
            m_probeModelCombo->clear();
            m_probeModelCombo->addItem(T("(select a model)"), QString());
            for (const QString &name : snapshot.installedNames)
                m_probeModelCombo->addItem(name, name);
            const int index = previous.isEmpty() ? -1 : m_probeModelCombo->findText(previous);
            m_probeModelCombo->setCurrentIndex(index >= 0 ? index : 0);
            m_probeModelCombo->blockSignals(false);
        }
    }

    updateClientTable(snapshot.clients);
    updateModelTable(snapshot.models);
    updateTrayTooltip();
}

void MainWindow::onStats(const ProxyStats &stats)
{
    m_lastStats = stats;
    m_lbTokens->setText(QString::number(stats.generatedTokens));

    // Do not overwrite a fresh benchmark result with proxy values.
    if (m_probeLockLabels) return;
    m_lbTtft->setText(stats.lastTtftMs > 0 ? QString::number(stats.lastTtftMs, 'f', 1)
                                           : QStringLiteral("—"));
    m_lbTps->setText(stats.lastTokensPerSec > 0
                         ? QString::number(stats.lastTokensPerSec, 'f', 1)
                         : QStringLiteral("—"));
}

void MainWindow::onProbeClick()
{
    if (!m_proc->isRunning()) {
        m_logModel->append(OllamaProcess::LogLevel::Warn,
                           T("Ollama is not running. Start the service before benchmarking."));
        statusBar()->showMessage(T("Benchmark failed: Ollama is not running"), 4000);
        return;
    }
    if (!m_probeModelCombo || m_probeModelCombo->currentIndex() <= 0) {
        m_logModel->append(OllamaProcess::LogLevel::Warn,
                           T("Select a locally installed model in the benchmark list first."));
        statusBar()->showMessage(T("Benchmark failed: select a model first"), 4000);
        return;
    }

    const QString model = m_probeModelCombo->currentData().toString();
    if (model.isEmpty()) {
        m_logModel->append(OllamaProcess::LogLevel::Warn, T("The selected model is invalid."));
        return;
    }

    m_probeBtn->setText(T("Benchmarking..."));
    m_probeBtn->setEnabled(false);
    m_probeLockLabels = true;
    m_lbTtft->setText(T("Benchmarking..."));
    m_lbTps->setText(T("Benchmarking..."));

    m_logModel->append(OllamaProcess::LogLevel::Info,
                       T("Benchmarking TTFT with model %1...").arg(model));
    statusBar()->showMessage(
        T("Benchmarking: loading the model and generating the first token..."), 8000);
    m_monitor->probeTtft(model);
}

void MainWindow::onProbeFinished(double ttftMs, qint64 tokens, double tokensPerSec,
                                 const QString &error)
{
    m_probeBtn->setText(T("Benchmark now (TTFT)"));
    m_probeBtn->setEnabled(true);

    // Keep the result visible for a few seconds before the proxy takes over.
    QTimer::singleShot(10000, this, [this]() { m_probeLockLabels = false; });

    if (!error.isEmpty()) {
        m_logModel->append(OllamaProcess::LogLevel::Warn, T("Benchmark failed: ") + error);
        m_lbTtft->setText(QStringLiteral("—"));
        m_lbTps->setText(QStringLiteral("—"));
        statusBar()->showMessage(T("Benchmark failed: ") + error, 6000);
        return;
    }

    // Defensive: a void result must never leave the labels on "Benchmarking...".
    if (ttftMs <= 0.0 && tokensPerSec <= 0.0) {
        m_lbTtft->setText(QStringLiteral("—"));
        m_lbTps->setText(QStringLiteral("—"));
        const QString reason = T("No valid data (the model did not load or the response was "
                                 "empty). Check the log.");
        m_logModel->append(OllamaProcess::LogLevel::Warn, reason);
        statusBar()->showMessage(reason, 6000);
        return;
    }

    const QString message = T("Result: TTFT=%1ms, %2 tokens, %3 tok/s")
                                .arg(ttftMs, 0, 'f', 1)
                                .arg(tokens)
                                .arg(tokensPerSec, 0, 'f', 1);
    m_logModel->append(OllamaProcess::LogLevel::Info, message);

    if (ttftMs > 0) m_lbTtft->setText(QString::number(ttftMs, 'f', 1));
    if (tokensPerSec > 0) m_lbTps->setText(QString::number(tokensPerSec, 'f', 1));
    statusBar()->showMessage(message, 8000);
}

void MainWindow::onRefreshTimer()
{
    m_monitor->setProcessPid(m_proc->pid());
    m_monitor->refresh();

    if (m_startedAt.isValid()) {
        const qint64 seconds = m_startedAt.secsTo(QDateTime::currentDateTime());
        m_uptimeLabel->setText(T("Uptime %1m %2s").arg(seconds / 60).arg(seconds % 60));
    }
}

void MainWindow::onLogFilterChanged()
{
    const int level = m_logLevelCombo->currentData().toInt();
    m_logModel->setFilter(static_cast<OllamaProcess::LogLevel>(level),
                          m_logKeyword->text(),
                          m_hideAccessCheck && m_hideAccessCheck->isChecked());
}

void MainWindow::onCopySelectedLog()
{
    const QModelIndexList rows = m_logView->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    QStringList lines;
    for (const QModelIndex &index : rows)
        lines << m_logModel->data(index, Qt::DisplayRole).toString();
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    statusBar()->showMessage(T("Copied %1 log lines").arg(lines.size()), 2500);
}

void MainWindow::onCopyAllLogs()
{
    const QString text = m_logModel->toPlainText();
    if (text.isEmpty()) return;
    QApplication::clipboard()->setText(text);
    statusBar()->showMessage(T("Copied all logs"), 2500);
}

void MainWindow::onResetStats()
{
    m_proxy->resetStats();
    m_logModel->append(OllamaProcess::LogLevel::Info, T("Statistics counters were reset."));
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(m_cfg, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString code = m_cfg->data().language;
    if (i18n::code(i18n::language()) != code) {
        i18n::setLanguage(i18n::fromCode(code));
        i18n::applyFont();
        retranslateUi();
    }
    applyConfigAfterSave();
}

void MainWindow::applyConfigAfterSave()
{
    const LauncherConfig &cfg = m_cfg->data();
    m_monitor->setConfig(cfg);
    m_refreshTimer->setInterval(cfg.refreshMs);
    updateAddressLabels();
    updateTrayTooltip();

    m_logModel->append(OllamaProcess::LogLevel::Info,
                       T("Settings saved to %1").arg(cfg.configPath));
    statusBar()->showMessage(
        T("Settings saved. Restart the service (and the app for proxy changes) to apply."), 6000);
}

void MainWindow::onOpenConfigFile()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_cfg->data().configPath));
}

void MainWindow::onExportLog()
{
    const QString path = QFileDialog::getSaveFileName(
        this, T("Export logs"), QStringLiteral("ollama_launcher_log.txt"),
        T("Text files (*.txt)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, T("Export failed"), file.errorString());
        return;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    out << m_logModel->toPlainText();
}

void MainWindow::updateClientTable(const QVector<TcpClient> &clients)
{
    if (!m_clientTable) return;

    QString signature;
    for (const TcpClient &client : clients) {
        signature += QStringLiteral("%1|%2|%3|%4;")
                         .arg(client.pid)
                         .arg(client.name)
                         .arg(client.remoteAddress)
                         .arg(client.remotePort);
    }
    if (signature == m_clientSignature) return;
    m_clientSignature = signature;

    m_clientTable->setRowCount(clients.size());

    for (int i = 0; i < clients.size(); ++i) {
        const TcpClient &client = clients.at(i);
        auto set = [this, i](int column, const QString &text) {
            m_clientTable->setItem(i, column, new QTableWidgetItem(text));
        };
        set(0, QString::number(client.pid));
        set(1, client.name);
        set(2, client.path);
        set(3, client.remoteAddress);
        set(4, QString::number(client.remotePort));
    }
}

void MainWindow::updateModelTable(const QVector<RunningModel> &models)
{
    if (!m_modelTable) return;

    QString signature;
    for (const RunningModel &model : models) {
        signature += QStringLiteral("%1|%2|%3|%4;")
                         .arg(model.name)
                         .arg(model.sizeMb)
                         .arg(model.vramMb)
                         .arg(model.expiresAt);
    }
    if (signature == m_modelSignature) return;
    m_modelSignature = signature;

    m_modelTable->setRowCount(models.size());

    for (int i = 0; i < models.size(); ++i) {
        const RunningModel &model = models.at(i);
        m_modelTable->setItem(i, 0, new QTableWidgetItem(model.name));
        m_modelTable->setItem(i, 1, new QTableWidgetItem(QString::number(model.sizeMb, 'f', 1)));
        m_modelTable->setItem(i, 2, new QTableWidgetItem(QString::number(model.vramMb, 'f', 1)));
        m_modelTable->setItem(i, 3, new QTableWidgetItem(model.expiresAt));
    }
}

void MainWindow::updateStatusPill(bool running, const QString &text)
{
    if (!m_statusPill) return;
    m_statusPill->setText(text);
    m_statusPill->setProperty("running", running);
    m_statusPill->style()->unpolish(m_statusPill);
    m_statusPill->style()->polish(m_statusPill);
}

void MainWindow::updateTrayTooltip()
{
    if (!m_tray) return;
    m_tray->setToolTip(T("Ollama Launcher · %1\nModels %2 / %3 · Clients %4 · Tokens %5")
                           .arg(m_proc->isRunning() ? T("running") : T("stopped"))
                           .arg(m_lastSnapshot.runningModels)
                           .arg(m_lastSnapshot.installedModels)
                           .arg(m_lastSnapshot.clients.size())
                           .arg(m_lastStats.generatedTokens));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_cfg->data().closeToTray && m_tray && m_tray->isVisible()) {
        if (!m_trayHintShown) {
            m_tray->showMessage(QStringLiteral("Ollama Launcher"),
                                T("Minimized to the tray. Double-click the tray icon to reopen."),
                                QSystemTrayIcon::Information, 3000);
            m_trayHintShown = true;
        }
        hide();
        event->ignore();
        return;
    }
    event->accept();
}
