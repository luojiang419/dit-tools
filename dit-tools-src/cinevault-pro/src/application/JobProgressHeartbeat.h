#pragma once

#include "core/jobs/JobEngine.h"
#include "domain/Entities.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

class JobProgressHeartbeat final : public QObject {
public:
    explicit JobProgressHeartbeat(JobEngine *jobEngine, QObject *parent = nullptr)
        : QObject(parent)
        , m_jobEngine(jobEngine)
    {
        m_timer.setInterval(3000);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            publishAll();
        });
    }

    void start(const QString &projectDatabasePath,
               qint64 jobId,
               qint64 progress,
               const QString &detailPrefix,
               const JobProgressContext &context,
               const QString &waitLabel)
    {
        if (!m_jobEngine || projectDatabasePath.trimmed().isEmpty() || jobId <= 0) {
            return;
        }

        Entry entry;
        entry.projectDatabasePath = projectDatabasePath;
        entry.jobId = jobId;
        entry.progress = qBound<qint64>(qint64{0}, progress, qint64{100});
        entry.detailPrefix = detailPrefix.trimmed();
        entry.context = context;
        entry.waitLabel = waitLabel.trimmed();
        entry.startedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_entries.insert(jobId, entry);
        publish(m_entries[jobId]);
        if (!m_timer.isActive()) {
            m_timer.start();
        }
    }

    void stop(qint64 jobId)
    {
        m_entries.remove(jobId);
        if (m_entries.isEmpty()) {
            m_timer.stop();
        }
    }

    void stopAll()
    {
        m_entries.clear();
        m_timer.stop();
    }

private:
    struct Entry {
        QString projectDatabasePath;
        qint64 jobId = 0;
        qint64 progress = 0;
        QString detailPrefix;
        JobProgressContext context;
        QString waitLabel;
        qint64 startedAtMs = 0;
    };

    QString waitText(const Entry &entry) const
    {
        const auto elapsedSeconds = qMax<qint64>(
            0, (QDateTime::currentMSecsSinceEpoch() - entry.startedAtMs) / 1000);
        const auto label = entry.waitLabel.isEmpty()
            ? QStringLiteral("处理中")
            : entry.waitLabel;
        return elapsedSeconds <= 0
            ? QStringLiteral("%1已启动").arg(label)
            : QStringLiteral("%1已等待 %2 秒").arg(label).arg(elapsedSeconds);
    }

    void publish(const Entry &entry)
    {
        auto context = entry.context;
        const auto heartbeatText = waitText(entry);
        if (!context.extraLabel.trimmed().isEmpty()) {
            context.extraLabel = QStringLiteral("%1 · %2")
                                     .arg(context.extraLabel.trimmed(), heartbeatText);
        } else {
            context.extraLabel = heartbeatText;
        }

        const auto detail = entry.detailPrefix.isEmpty()
            ? heartbeatText
            : QStringLiteral("%1，%2").arg(entry.detailPrefix, heartbeatText);
        m_jobEngine->updateJobForProject(
            entry.projectDatabasePath, entry.jobId, entry.progress, detail, context);
    }

    void publishAll()
    {
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
            publish(it.value());
        }
    }

    JobEngine *m_jobEngine = nullptr;
    QTimer m_timer;
    QHash<qint64, Entry> m_entries;
};
