#pragma once

#include "domain/Entities.h"

#include <QFutureSynchronizer>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <atomic>

class DatabaseManager;
class JobEngine;
class IndexingWorkCoordinator;
class MediaProbeEngine;
class ThumbnailEngine;
class QSqlDatabase;

class ScanEngine : public QObject {
    Q_OBJECT

public:
    explicit ScanEngine(DatabaseManager *databaseManager, JobEngine *jobEngine, MediaProbeEngine *mediaProbeEngine, ThumbnailEngine *thumbnailEngine, QObject *parent = nullptr);

    static constexpr int CurrentScanVersion = 5;

    void startScan(const SourceRoot &sourceRoot,
                   qint64 jobId,
                   const QStringList &dirtyDirectoryPaths = {});
    void requestCancel(qint64 sourceRootId);
    void requestCancelAll();
    void waitForIdle();
    void setWorkCoordinator(IndexingWorkCoordinator *workCoordinator);
    void setFailureAfterEntriesForTesting(qint64 entryCount);

signals:
    void scanBatchCommitted(const ScanBatch &batch);
    void scanFinished(qint64 sourceRootId);
    void scanFailed(qint64 sourceRootId, const QString &message);
    void scanFinishedForProject(const QString &projectDatabasePath, qint64 sourceRootId);
    void scanFailedForProject(const QString &projectDatabasePath,
                              qint64 sourceRootId,
                              const QString &message);

private:
    void runScan(SourceRoot sourceRoot,
                 qint64 jobId,
                 const QString &projectDatabasePath,
                 const QString &activeScanKey,
                 qint64 sessionId,
                 quint64 workGeneration,
                 bool fullScan,
                 QStringList dirtyRelativePaths);
    void runResumableScan(SourceRoot sourceRoot,
                          qint64 jobId,
                          const QString &projectDatabasePath,
                          qint64 sessionId,
                          QSqlDatabase &database,
                          quint64 workGeneration,
                          bool fullScan,
                          const QStringList &dirtyRelativePaths);
    qint64 prepareSession(const SourceRoot &sourceRoot,
                          const QStringList &dirtyDirectoryPaths,
                          QStringList *dirtyRelativePaths,
                          QString *errorMessage);
    void releaseActiveScan(const QString &activeScanKey, qint64 sourceRootId);
    bool isCancellationRequested(qint64 sourceRootId);

    DatabaseManager *m_databaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    MediaProbeEngine *m_mediaProbeEngine = nullptr;
    ThumbnailEngine *m_thumbnailEngine = nullptr;
    IndexingWorkCoordinator *m_workCoordinator = nullptr;
    QFutureSynchronizer<void> m_scanFutures;
    QMutex m_activeScansMutex;
    QSet<QString> m_activeScans;
    QSet<qint64> m_cancelledSourceRoots;
    std::atomic<qint64> m_failureAfterEntries{-1};
};
