#include "log_model.h"

#include <QFont>

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return QVariant();
    const LogEntry &entry = m_all.at(m_visible.at(index.row()));

    switch (role) {
    case Qt::DisplayRole: {
        static const char *kTags[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
        const int level = static_cast<int>(entry.level);
        return QStringLiteral("%1  [%2]  %3")
            .arg(entry.time.toString(QStringLiteral("HH:mm:ss.zzz")))
            .arg(QLatin1String(kTags[level >= 0 && level < 4 ? level : 1]))
            .arg(entry.text);
    }
    case Qt::ForegroundRole:
        return colorFor(entry.level);
    case Qt::FontRole: {
        QFont font;
        font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
        font.setStyleHint(QFont::Monospace);
        font.setPointSize(9);
        if (entry.level == OllamaProcess::LogLevel::Error) font.setBold(true);
        return font;
    }
    case Qt::ToolTipRole:
        return entry.time.toString(Qt::ISODateWithMs) + QLatin1Char('\n') + entry.text;
    default:
        return QVariant();
    }
}

void LogModel::append(OllamaProcess::LogLevel level, const QString &text)
{
    // A reset keeps indices trivially correct; with 3000 lines the cost is
    // negligible compared to the streaming rate of the log.
    beginResetModel();
    m_all.append(LogEntry{QDateTime::currentDateTime(), level, text});
    if (level == OllamaProcess::LogLevel::Error) ++m_errorCount;
    if (level == OllamaProcess::LogLevel::Warn) ++m_warnCount;

    if (m_all.size() > m_maxLines) m_all.remove(0, m_all.size() - m_maxLines);

    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (accepted(m_all.at(i))) m_visible.append(i);
    }
    endResetModel();
}

void LogModel::clear()
{
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    m_errorCount = 0;
    m_warnCount = 0;
    endResetModel();
}

void LogModel::setFilter(OllamaProcess::LogLevel minLevel, const QString &keyword, bool hideAccess)
{
    beginResetModel();
    m_minLevel = minLevel;
    m_keyword = keyword.trimmed();
    m_hideAccess = hideAccess;

    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (accepted(m_all.at(i))) m_visible.append(i);
    }
    endResetModel();
}

QString LogModel::toPlainText() const
{
    QString out;
    for (int row = 0; row < m_visible.size(); ++row)
        out += data(index(row, 0), Qt::DisplayRole).toString() + QLatin1Char('\n');
    return out;
}

QColor LogModel::colorFor(OllamaProcess::LogLevel level)
{
    switch (level) {
    case OllamaProcess::LogLevel::Error: return QColor(QStringLiteral("#dc2626"));
    case OllamaProcess::LogLevel::Warn:  return QColor(QStringLiteral("#b45309"));
    case OllamaProcess::LogLevel::Info:  return QColor(QStringLiteral("#334155"));
    case OllamaProcess::LogLevel::Debug: return QColor(QStringLiteral("#94a3b8"));
    }
    return QColor(QStringLiteral("#334155"));
}

bool LogModel::accepted(const LogEntry &entry) const
{
    if (entry.level < m_minLevel) return false;
    if (m_hideAccess && entry.text.contains(QStringLiteral("[GIN]"))) return false;
    if (!m_keyword.isEmpty() && !entry.text.contains(m_keyword, Qt::CaseInsensitive)) return false;
    return true;
}
