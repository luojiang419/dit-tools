#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QFuture>
#include <QHash>
#include <QTimer>
#include <QThreadPool>
#include <QVariantList>
#include <QVector>

class DatabaseManager;
class IndexingWorkCoordinator;

class JobEngine : public QObject {
    Q_OBJECT

public:
    explicit JobEngine(DatabaseManager *databaseManager, QObject *parent = nullptr);

    void setWorkCoordinator(IndexingWorkCoordinator *workCoordinator);

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
    bool removeFinishedJob(qint64 jobId);
    void clearFinishedJobs();
    void clearFailedJobsForRetry(qint64 sourceRootId, const QVector<JobType> &types);

    QVector<Job> jobs() const;
    void waitForPersistence();

signals:
    void jobsChanged();
    void persistenceError(const QString &errorMessage);

private:
    struct PendingPersistence {
        QString key;
        QString projectDatabasePath;
        QString statement;
        QVariantList values;
        int attempt = 0;
    };

    struct PersistenceBatchResult {
        QVector<PendingPersistence> failed;
        QString errorMessage;
    };

    bool persistJob(const Job &job);
    void enqueuePersistence(const QString &key,
                            const QString &projectDatabasePath,
                            const QString &statement,
                            const QVariantList &values,
                            bool flushImmediately = false);
    void flushPendingPersistence();
    void startPersistenceBatch(QVector<PendingPersistence> batch, bool observeCompletion);
    PersistenceBatchResult executePersistenceBatch(QVector<PendingPersistence> batch) const;
    void handlePersistenceResult(const PersistenceBatchResult &result);
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
    IndexingWorkCoordinator *m_workCoordinator = nullptr;
    QVector<Job> m_jobs;
    qint64 m_nextId = 1;
    QHash<QString, PendingPersistence> m_pendingPersistence;
    QTimer m_persistenceTimer;
    QThreadPool m_persistencePool;
    QFuture<PersistenceBatchResult> m_persistenceFuture;
    bool m_persistenceRunning = false;
};
