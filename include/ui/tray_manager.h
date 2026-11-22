// System tray icon, context menu and tooltip. The icon is painted in code, so
// the app ships without external image files.
#ifndef OL_UI_TRAY_MANAGER_H
#define OL_UI_TRAY_MANAGER_H

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;

class TrayManager : public QObject
{
    Q_OBJECT

public:
    explicit TrayManager(QSystemTrayIcon *tray, QObject *parent = nullptr);
    ~TrayManager() override;

    void setRunning(bool running);
    void setTooltip(const QString &text);

    // Re-applies all menu texts after a language change.
    void retranslateUi();

    static QIcon makeIcon(bool running);

signals:
    void showRequested();
    void startRequested();
    void stopRequested();
    void quitRequested();

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_quitAction = nullptr;
};

#endif // OL_UI_TRAY_MANAGER_H
