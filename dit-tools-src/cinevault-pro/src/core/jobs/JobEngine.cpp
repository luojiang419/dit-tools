#include "core/jobs/JobEngine.h"

#include "infrastructure/db/DatabaseManager.h"

#include <QDateTime>
#include <QSqlQuery>

namespace {
bool hasProgressContext(const JobProgressContext &context)
{
    return context.currentStep > 0
        || context.totalSteps > 0
        || !context.stepLabel.trimmed().isEmpty()
        || context.currentItem > 0
        || context.totalItems > 0
        || !context.unitLabel.trimmed().isEmpty()
        || context.currentFrameNumber > 0
        || !context.extraLabel.trimmed().isEmpty();
}
}

JobEngine::JobEngine(DatabaseManager *databaseManager, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
{
}

qint64 JobEngine::createJob(JobType type,
                            const QString &title,
                            const QString &detail,
                            qint64 sourceRootId,
                            const JobSubject &subject,
                            const JobProgressContext &progressContext)
{
    return appendJob(type, JobState::Running, title, detail, sourceRootId, subject, progressContext);
}

qint64 JobEngine::queueJob(JobType type,
                           const QString &title,
                           const QString &detail,
                           qint64 sourceRootId,
                           const JobSubject &subject,
                           const JobProgressContext &progressContext)
{
    return appendJob(type, JobState::Pending, title, detail, sourceRootId, subject, progressContext);
}

qint64 JobEngine::appendJob(JobType type,
                            JobState state,
                            const QString &title,
                            const QString &detail,
                            qint64 sourceRootId,
                            const JobSubject &subject,
                            const JobProgressContext &progressContext)
{
    Job job;
    job.id = m_nextId++;
    job.type = type;
    job.state = state;
    job.title = title;
    job.detail = detail;
    job.progress = state == JobState::Pending ? 0 : 0;
    job.sourceRootId = sourceRootId;
    job.subject = subject;
    job.progressContext = progressContext;
    job.startedAt = QDateTime::currentDateTime();
    job.updatedAt = job.startedAt;
    m_jobs.prepend(job);
    persistJob(job);
    emit jobsChanged();
    return job.id;
}

void JobEngine::updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext)
{
    if (auto *job = findJob(jobId)) {
        job->progress = progress;
        job->detail = detail;
        if (hasProgressContext(progressContext)) {
            job->progressContext = progressContext;
        }
        job->updatedAt = QDateTime::currentDateTime();
        persistJob(*job);
        emit jobsChanged();
    }
}

void JobEngine::updateJobSubject(qint64 jobId, const JobSubject &subject)
{
    if (auto *job = findJob(jobId)) {
        job->subject = subject;
        job->updatedAt = QDateTime::currentDateTime();
        emit jobsChanged();
    }
}

void JobEngine::completeJob(qint64 jobId, const QString &detail)
{
    if (auto *job = findJob(jobId)) {
        job->state = JobState::Completed;
        job->progress = 100;
        job->detail = detail;
        job->updatedAt = QDateTime::currentDateTime();
        persistJob(*job);
        emit jobsChanged();
    }
}

void JobEngine::failJob(qint64 jobId, const QString &errorMessage)
{
    if (auto *job = findJob(jobId)) {
        job->state = JobState::Failed;
        job->errorMessage = errorMessage;
        job->detail = errorMessage;
        job->updatedAt = QDateTime::currentDateTime();
        persistJob(*job);
        emit jobsChanged();
    }
}

void JobEngine::clearJobs()
{
    if (m_jobs.isEmpty()) {
        return;
    }
    m_jobs.clear();
    emit jobsChanged();
}

void JobEngine::clearCompletedJobs()
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
    if (m_databaseManager && m_databaseManager->hasOpenProject()) {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("DELETE FROM job WHERE state = ?"));
        query.addBindValue(static_cast<int>(JobState::Completed));
        query.exec();
    }
    emit jobsChanged();
}

QVector<Job> JobEngine::jobs() const
{
    return m_jobs;
}

void JobEngine::persistJob(const Job &job)
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO job (id, type, state, title, detail, error_message, progress, source_root_id, started_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(job.id);
    query.addBindValue(static_cast<int>(job.type));
    query.addBindValue(static_cast<int>(job.state));
    query.addBindValue(job.title);
    query.addBindValue(job.detail);
    query.addBindValue(job.errorMessage);
    query.addBindValue(job.progress);
    query.addBindValue(job.sourceRootId);
    query.addBindValue(job.startedAt.toString(Qt::ISODate));
    query.addBindValue(job.updatedAt.toString(Qt::ISODate));
    query.exec();
}

Job *JobEngine::findJob(qint64 jobId)
{
    for (auto &job : m_jobs) {
        if (job.id == jobId) {
            return &job;
        }
    }
    return nullptr;
}
