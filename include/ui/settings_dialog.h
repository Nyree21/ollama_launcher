// Modal settings editor. Every option of ollama_launcher.ini can be edited here;
// the main window only keeps a button that opens this dialog.
#ifndef OL_UI_SETTINGS_DIALOG_H
#define OL_UI_SETTINGS_DIALOG_H

#include <QDialog>

#include "app_config.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(AppConfig *cfg, QWidget *parent = nullptr);

    // Validate, write back to the config object, save the .ini and close.
    void accept() override;

private slots:
    void browseOllama();
    void browseModels();

private:
    // Adds a "label + field" row followed by an optional help line.
    void addField(QFormLayout *form, const QString &label, QWidget *field,
                  const QString &help);
    void loadFrom(const LauncherConfig &cfg);

    AppConfig *m_cfg = nullptr;

    QLineEdit *m_edOllamaPath = nullptr;
    QLineEdit *m_edModelsDir = nullptr;
    QLineEdit *m_edHost = nullptr;
    QSpinBox *m_spPort = nullptr;
    QLineEdit *m_edExtraArgs = nullptr;
    QPlainTextEdit *m_edExtraEnv = nullptr;

    QCheckBox *m_ckProxyEnabled = nullptr;
    QSpinBox *m_spProxyPort = nullptr;

    QCheckBox *m_ckAutoStart = nullptr;
    QCheckBox *m_ckCloseToTray = nullptr;
    QSpinBox *m_spRefreshMs = nullptr;
    QComboBox *m_cbLogLevel = nullptr;
    QComboBox *m_cbLanguage = nullptr;
};

#endif // OL_UI_SETTINGS_DIALOG_H
