#include "ui/viewmodels/MinimalJobTimelineViewModel.h"

#include "shared/Formatters.h"
#include "ui/models/JobListModel.h"

#include <QVariantMap>

MinimalJobTimelineViewModel::MinimalJobTimelineViewModel(QObject *parent)
    : QObject(parent)
    , m_model(new JobListModel(this))
{
    seedJobs();
    reload();
}

JobListModel *MinimalJobTimelineViewModel::model() const
{
    return m_model;
}

QVariantList MinimalJobTimelineViewModel::timelineItems() const
{
    return m_timelineItems;
}

bool MinimalJobTimelineViewModel::canClearCompletedJobs() const
{
    for (const auto &job : m_jobs) {
        if (job.state == JobState::Completed) {
            return true;
        }
    }
    return false;
}

void MinimalJobTimelineViewModel::reload()
{
    m_model->setItems(m_jobs);

    QVariantList rows;
    for (const auto &job : m_jobs) {
        QVariantMap row;
        row.insert(QStringLiteral("title"), job.title);
        row.insert(QStringLiteral("detail"), job.detail);
        row.insert(QStringLiteral("progress"), job.progress);
        row.insert(QStringLiteral("stateLabel"), Formatters::jobStateLabel(job.state));
        rows.append(row);
    }
    m_timelineItems = rows;
    emit timelineChanged();
}

void MinimalJobTimelineViewModel::clearCompletedJobs()
{
    QVector<Job> keptJobs;
    keptJobs.reserve(m_jobs.size());
    for (const auto &job : m_jobs) {
        if (job.state != JobState::Completed) {
            keptJobs.append(job);
        }
    }
    if (keptJobs.size() == m_jobs.size()) {
        return;
    }
    m_jobs = keptJobs;
    reload();
}

void MinimalJobTimelineViewModel::seedJobs()
{
    const auto now = QDateTime::currentDateTime();
    m_jobs = {
        Job{1, JobType::Scan, JobState::Completed, QStringLiteral("扫描 A001"), QStringLiteral("首测包中为演示数据"), QString(), 100, 1, now, now},
        Job{2, JobType::Metadata, JobState::Pending, QStringLiteral("元数据队列"), QStringLiteral("将在后续里程碑接回"), QString(), 0, 1, now, now},
        Job{3, JobType::Thumbnail, JobState::Pending, QStringLiteral("缩略图队列"), QStringLiteral("将在后续里程碑接回"), QString(), 0, 1, now, now}
    };
}
