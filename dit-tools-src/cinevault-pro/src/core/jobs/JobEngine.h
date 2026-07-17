#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QVector>

class DatabaseManager;

class JobEngine : public QObject {
    Q_OBJECT

public:
    explicit JobEngine(DatabaseManager *databaseManager, QObject *parent = nullptr);

    qint64 createJob(JobType type,
                     const QString &title,
                     const QString &detail,
                     qint64 sourceRootId = 0,
                     const JobSubject &subject = JobSubject(),
                     const JobProgressContext &progressContext = JobProgressContext());
    qint64 queueJob(JobType type,
                    const QString &title,
                    const QString &detail,
                    qint64 sourceRootId = 0,
                    const JobSubject &subject = JobSubject(),
                    const JobProgressContext &progressContext = JobProgressContext());
    void updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext = JobProgressContext());
    void updateJobSubject(qint64 jobId, const JobSubject &subject);
    void completeJob(qint64 jobId, const QString &detail);
    void failJob(qint64 jobId, const QString &errorMessage);
    QString currentProjectDatabasePath() const;
    void updateJobForProject(const QString &projectDatabasePath,
                             qint64 jobId,
                             qint64 progress,
                             const QString &detail,
                             const JobProgressContext &progressContext = JobProgressContext());
    void updateJobSubjectForProject(const QString &projectDatabasePath,
                                    qint64 jobId,
                                    const JobSubject &subject);
    void completeJobForProject(const QString &projectDatabasePath, qint64 jobId, const QString &detail);
    void failJobForProject(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage);
    void reloadJobs();
    void clearJobs();
    void clearCompletedJobs();

    QVector<Job> jobs() const;

signals:
    void jobsChanged();
    void persistenceError(const QString &errorMessage);

private:
    bool persistJob(const Job &job);
    void reportPersistenceError(const QString &errorMessage);
    Job *findJob(qint64 jobId);
    bool isCurrentProject(const QString &projectDatabasePath) const;
    qint64 appendJob(JobType type,
                     JobState state,
                     const QString &title,
                     const QString &detail,
                     qint64 sourceRootId,
                     const JobSubject &subject,
                     const JobProgressContext &progressContext);

    DatabaseManager *m_databaseManager = nullptr;
    QVector<Job> m_jobs;
    qint64 m_nextId = 1;
};
