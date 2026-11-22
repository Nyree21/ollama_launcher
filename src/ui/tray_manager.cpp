#include "tray_manager.h"

#include "i18n.h"

#include <QAction>
#include <QBrush>
#include <QFont>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

TrayManager::TrayManager(QSystemTrayIcon *tray, QObject *parent)
    : QObject(parent), m_tray(tray)
{
    m_menu = new QMenu();

    m_showAction = m_menu->addAction(QString());
    connect(m_showAction, &QAction::triggered, this, &TrayManager::showRequested);
    m_menu->addSeparator();

    m_startAction = m_menu->addAction(QString());
    connect(m_startAction, &QAction::triggered, this, &TrayManager::startRequested);

    m_stopAction = m_menu->addAction(QString());
    connect(m_stopAction, &QAction::triggered, this, &TrayManager::stopRequested);
    m_menu->addSeparator();

    m_quitAction = m_menu->addAction(QString());
    connect(m_quitAction, &QAction::triggered, this, &TrayManager::quitRequested);

    m_tray->setContextMenu(m_menu);
    m_tray->setIcon(makeIcon(false));
    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayManager::onActivated);

    retranslateUi();
    setRunning(false);
}

TrayManager::~TrayManager()
{
    // The menu has no QWidget parent, so it is owned by this object.
    if (m_tray) m_tray->setContextMenu(nullptr);
    delete m_menu;
}

void TrayManager::retranslateUi()
{
    m_showAction->setText(T("Show / hide window"));
    m_startAction->setText(T("Start Ollama"));
    m_stopAction->setText(T("Stop Ollama"));
    m_quitAction->setText(T("Quit Ollama Launcher"));
    m_tray->setToolTip(QStringLiteral("Ollama Launcher"));
}

void TrayManager::setRunning(bool running)
{
    m_tray->setIcon(makeIcon(running));
    m_startAction->setEnabled(!running);
    m_stopAction->setEnabled(running);
}

void TrayManager::setTooltip(const QString &text)
{
    m_tray->setToolTip(text);
}

void TrayManager::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    // Double click opens the window; a single click keeps showing the menu.
    if (reason == QSystemTrayIcon::DoubleClick
#if defined(Q_OS_MACOS)
        || reason == QSystemTrayIcon::Trigger
#endif
        || reason == QSystemTrayIcon::MiddleClick) {
        emit showRequested();
    }
}

QIcon TrayManager::makeIcon(bool running)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Blue rounded tile with a white "O".
    QLinearGradient gradient(0, 0, 64, 64);
    gradient.setColorAt(0.0, QColor(QStringLiteral("#3b82f6")));
    gradient.setColorAt(1.0, QColor(QStringLiteral("#1d4ed8")));
    painter.setBrush(QBrush(gradient));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(4, 4, 56, 56), 16, 16);

    QFont font;
    font.setPointSize(30);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#ffffff")));
    painter.drawText(QRect(4, 4, 56, 56), Qt::AlignCenter, QStringLiteral("O"));

    // Status dot: green running, grey stopped.
    painter.setBrush(QColor(running ? QStringLiteral("#22c55e") : QStringLiteral("#94a3b8")));
    painter.setPen(QColor(QStringLiteral("#ffffff")));
    painter.drawEllipse(QRect(40, 40, 18, 18));
    painter.end();

    return QIcon(pixmap);
}
