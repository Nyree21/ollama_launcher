// Flat log model: one line per entry, colour-coded by level, with level and
// keyword filtering.
#ifndef OL_UI_LOG_MODEL_H
#define OL_UI_LOG_MODEL_H

#include <QAbstractListModel>
#include <QColor>
#include <QDateTime>
#include <QString>
#include <QVector>

#include "ollama_process.h"

struct LogEntry
{
    QDateTime time;
    OllamaProcess::LogLevel level = OllamaProcess::LogLevel::Info;
    QString text;
};

class LogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void append(OllamaProcess::LogLevel level, const QString &text);
    void clear();

    // `hideAccess` drops the [GIN] probe-request lines that appear on every poll.
    void setFilter(OllamaProcess::LogLevel minLevel, const QString &keyword,
                   bool hideAccess = false);

    QString toPlainText() const;

    int errorCount() const { return m_errorCount; }
    int warnCount() const { return m_warnCount; }

    static QColor colorFor(OllamaProcess::LogLevel level);
    void setMaxLines(int lines) { m_maxLines = lines; }

private:
    bool accepted(const LogEntry &entry) const;

    QVector<LogEntry> m_all;
    QVector<int> m_visible; // indices into m_all, in display order
    OllamaProcess::LogLevel m_minLevel = OllamaProcess::LogLevel::Debug;
    QString m_keyword;
    bool m_hideAccess = false;
    int m_maxLines = 3000;
    int m_errorCount = 0;
    int m_warnCount = 0;
};

#endif // OL_UI_LOG_MODEL_H
