#pragma once

#include "domain/Entities.h"

#include <QFutureSynchronizer>
#include <QObject>

class DatabaseManager;
class JobEngine;
class MediaProbeEngine;
class ThumbnailEngine;

class ScanEngine : public QObject {
    Q_OBJECT

public:
    explicit ScanEngine(DatabaseManager *databaseManager, JobEngine *jobEngine, MediaProbeEngine *mediaProbeEngine, ThumbnailEngine *thumbnailEngine, QObject *parent = nullptr);

    static constexpr int CurrentScanVersion = 2;

    void startScan(const SourceRoot &sourceRoot, qint64 jobId);
    void waitForIdle();

signals:
    void scanBatchCommitted(const ScanBatch &batch);
    void scanFinished(qint64 sourceRootId);
    void scanFailed(qint64 sourceRootId, const QString &message);

private:
    void runScan(SourceRoot sourceRoot, qint64 jobId);

    DatabaseManager *m_databaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    MediaProbeEngine *m_mediaProbeEngine = nullptr;
    ThumbnailEngine *m_thumbnailEngine = nullptr;
    QFutureSynchronizer<void> m_scanFutures;
};
