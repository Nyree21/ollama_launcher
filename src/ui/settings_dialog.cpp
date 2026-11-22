#include "settings_dialog.h"

#include "i18n.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
// Builds a card with a bold section title and returns the form layout inside it.
QFormLayout *addSection(QVBoxLayout *parentLayout, const QString &title, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(8);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("groupTitle"));
    cardLayout->addWidget(titleLabel);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(4);
    cardLayout->addLayout(form);

    parentLayout->addWidget(card);
    return form;
}
} // namespace

SettingsDialog::SettingsDialog(AppConfig *cfg, QWidget *parent)
    : QDialog(parent), m_cfg(cfg)
{
    setWindowTitle(T("Settings · Ollama Launcher"));
    setWindowIcon(parent ? parent->windowIcon() : QIcon());
    setMinimumSize(QSize(720, 620));
    resize(QSize(820, 700));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(14);

    // ---------------------------------------------------------------- server
    QFormLayout *serverForm = addSection(lay, T("Ollama service"), content);

    m_edOllamaPath = new QLineEdit(content);
    m_edOllamaPath->setToolTip(T("Full path of ollama.exe."));
    auto *browseExe = new QPushButton(T("Browse..."), content);
    connect(browseExe, &QPushButton::clicked, this, &SettingsDialog::browseOllama);
    auto *exeRow = new QHBoxLayout();
    exeRow->addWidget(m_edOllamaPath, 1);
    exeRow->addWidget(browseExe);
    auto *exeWrap = new QWidget(content);
    exeWrap->setLayout(exeRow);
    addField(serverForm, T("ollama.exe path"), exeWrap,
             T("Executable to launch. Leave it empty to auto-detect a standard install or PATH."));

    m_edModelsDir = new QLineEdit(content);
    m_edModelsDir->setToolTip(T("Directory passed to the child process as OLLAMA_MODELS."));
    auto *browseDir = new QPushButton(T("Browse..."), content);
    connect(browseDir, &QPushButton::clicked, this, &SettingsDialog::browseModels);
    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(m_edModelsDir, 1);
    dirRow->addWidget(browseDir);
    auto *dirWrap = new QWidget(content);
    dirWrap->setLayout(dirRow);
    addField(serverForm, T("Models directory"), dirWrap,
             T("Maps to OLLAMA_MODELS. Leave it empty to use Ollama's default directory."));

    m_edHost = new QLineEdit(content);
    m_edHost->setToolTip(T("Address Ollama listens on, usually 127.0.0.1."));
    m_spPort = new QSpinBox(content);
    m_spPort->setRange(1, 65535);
    m_spPort->setToolTip(T("Ollama port, 11434 by default. It must differ from the proxy port."));
    auto *hostRow = new QHBoxLayout();
    hostRow->addWidget(m_edHost, 2);
    hostRow->addWidget(new QLabel(T("Port"), content));
    hostRow->addWidget(m_spPort, 1);
    auto *hostWrap = new QWidget(content);
    hostWrap->setLayout(hostRow);
    addField(serverForm, T("Listen address / port"), hostWrap,
             T("Sets OLLAMA_HOST=address:port. Restart the service after changing it."));

    m_edExtraArgs = new QLineEdit(content);
    m_edExtraArgs->setPlaceholderText(T("e.g. --verbose"));
    addField(serverForm, T("Extra arguments"), m_edExtraArgs,
             T("Appended verbatim to \"ollama serve\". Usually empty."));

    m_edExtraEnv = new QPlainTextEdit(content);
    m_edExtraEnv->setPlaceholderText(
        T("One KEY=VALUE per line, e.g.\nOLLAMA_IGPU_ENABLE=1\nOLLAMA_KEEP_ALIVE=30m"));
    m_edExtraEnv->setMaximumHeight(90);
    addField(serverForm, T("Extra environment variables"), m_edExtraEnv,
             T("For example OLLAMA_IGPU_ENABLE=1 for an Intel iGPU, or OLLAMA_KEEP_ALIVE=30m."));

    // ----------------------------------------------------------------- proxy
    QFormLayout *proxyForm = addSection(lay, T("Stats proxy (token / TTFT)"), content);

    m_ckProxyEnabled = new QCheckBox(T("Enable the stats proxy (recommended)"), content);
    addField(proxyForm, T("Enable proxy"), m_ckProxyEnabled,
             T("Ollama has no cumulative token counter, so the proxy parses responses "
               "while forwarding them. Metrics stay at 0 when disabled."));

    m_spProxyPort = new QSpinBox(content);
    m_spProxyPort->setRange(1, 65535);
    addField(proxyForm, T("Proxy port"), m_spProxyPort,
             T("Point the client's base URL to http://127.0.0.1:<port> to collect stats. "
               "Restart this app after changing it."));

    // ----------------------------------------------------------- interface
    QFormLayout *uiForm = addSection(lay, T("Interface and logs"), content);

    m_ckAutoStart = new QCheckBox(T("Start ollama serve when the launcher starts"), content);
    addField(uiForm, T("Auto start"), m_ckAutoStart,
             T("Starting the launcher also starts the Ollama service."));

    m_ckCloseToTray = new QCheckBox(T("Hide the window to the tray on close"), content);
    addField(uiForm, T("Tray behaviour"), m_ckCloseToTray,
             T("The close button then only hides the window. Quit from the tray menu."));

    m_spRefreshMs = new QSpinBox(content);
    m_spRefreshMs->setRange(500, 60000);
    m_spRefreshMs->setSingleStep(500);
    addField(uiForm, T("Refresh interval (ms)"), m_spRefreshMs,
             T("How often models, memory and connections are sampled. Very small values "
               "add probe traffic."));

    m_cbLogLevel = new QComboBox(content);
    m_cbLogLevel->addItems({QStringLiteral("debug"), QStringLiteral("info"),
                            QStringLiteral("warn"), QStringLiteral("error")});
    addField(uiForm, T("Log level"), m_cbLogLevel,
             T("The log tab can filter further by level and keyword."));

    m_cbLanguage = new QComboBox(content);
    // Language names stay in their own language on purpose.
    m_cbLanguage->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_cbLanguage->addItem(QStringLiteral("简体中文"), QStringLiteral("zh"));
    addField(uiForm, T("Language"), m_cbLanguage,
             T("Applies immediately after saving."));

    auto *note = new QLabel(
        T("Config file: %1 (nothing is written to the registry). Port and path changes "
          "need a service restart; proxy changes need an app restart.")
            .arg(m_cfg->data().configPath),
        content);
    note->setObjectName(QStringLiteral("cardHint"));
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addStretch(1);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("dlgButtons"));
    buttons->button(QDialogButtonBox::Ok)->setText(T("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The button box gets its own margins; otherwise it touches the dialog edge.
    auto *buttonRow = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(18, 10, 18, 14);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(buttons);
    root->addWidget(buttonRow);

    loadFrom(m_cfg->data());
}

void SettingsDialog::addField(QFormLayout *form, const QString &label, QWidget *field,
                              const QString &help)
{
    form->addRow(label, field);
    if (help.isEmpty()) return;

    auto *hint = new QLabel(help, field->parentWidget());
    hint->setObjectName(QStringLiteral("fieldHelp"));
    hint->setWordWrap(true);
    form->addRow(QString(), hint);
}

void SettingsDialog::loadFrom(const LauncherConfig &cfg)
{
    m_edOllamaPath->setText(cfg.ollamaPath);
    m_edModelsDir->setText(cfg.modelsDir);
    m_edHost->setText(cfg.host);
    m_spPort->setValue(cfg.port);
    m_edExtraArgs->setText(cfg.extraArgs);

    QStringList envLines;
    for (auto it = cfg.extraEnv.constBegin(); it != cfg.extraEnv.constEnd(); ++it)
        envLines.append(it.key() + QLatin1Char('=') + it.value());
    m_edExtraEnv->setPlainText(envLines.join(QLatin1Char('\n')));

    m_ckProxyEnabled->setChecked(cfg.proxyEnabled);
    m_spProxyPort->setValue(cfg.proxyPort);
    m_ckAutoStart->setChecked(cfg.autoStart);
    m_ckCloseToTray->setChecked(cfg.closeToTray);
    m_spRefreshMs->setValue(cfg.refreshMs);

    const int levelIndex = m_cbLogLevel->findText(cfg.logLevel);
    m_cbLogLevel->setCurrentIndex(levelIndex >= 0 ? levelIndex : 1);

    const int languageIndex = m_cbLanguage->findData(cfg.language);
    m_cbLanguage->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
}

void SettingsDialog::browseOllama()
{
    const QString path = QFileDialog::getOpenFileName(
        this, T("Select ollama.exe"), m_edOllamaPath->text(),
        T("Executable (*.exe);;All files (*.*)"));
    if (!path.isEmpty()) m_edOllamaPath->setText(path);
}

void SettingsDialog::browseModels()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, T("Select the models directory"), m_edModelsDir->text());
    if (!dir.isEmpty()) m_edModelsDir->setText(dir);
}

void SettingsDialog::accept()
{
    LauncherConfig cfg = m_cfg->data(); // start from the live config
    cfg.ollamaPath = m_edOllamaPath->text().trimmed();
    cfg.modelsDir = m_edModelsDir->text().trimmed();
    cfg.host = m_edHost->text().trimmed();
    cfg.port = static_cast<quint16>(m_spPort->value());
    cfg.extraArgs = m_edExtraArgs->text().trimmed();

    cfg.extraEnv.clear();
    const QStringList envLines = m_edExtraEnv->toPlainText().split(QLatin1Char('\n'));
    for (const QString &raw : envLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            QMessageBox::warning(this, T("Environment variable format error"),
                                 T("Use the KEY=VALUE format: ") + line);
            return;
        }
        cfg.extraEnv.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }

    cfg.proxyEnabled = m_ckProxyEnabled->isChecked();
    cfg.proxyPort = static_cast<quint16>(m_spProxyPort->value());
    cfg.autoStart = m_ckAutoStart->isChecked();
    cfg.closeToTray = m_ckCloseToTray->isChecked();
    cfg.refreshMs = m_spRefreshMs->value();
    cfg.logLevel = m_cbLogLevel->currentText();
    cfg.language = m_cbLanguage->currentData().toString();

    const LauncherConfig backup = m_cfg->data();
    m_cfg->data() = cfg;

    QString error;
    if (!m_cfg->validate(&error)) {
        m_cfg->data() = backup;
        QMessageBox::warning(this, T("Configuration validation failed"), error);
        return;
    }

    m_cfg->save();
    QDialog::accept();
}
