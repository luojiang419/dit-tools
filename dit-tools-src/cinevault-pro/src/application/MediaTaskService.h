#pragma once

#include "domain/Entities.h"

#include <QFutureSynchronizer>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QVector>

class DatabaseManager;
class JobEngine;
class MediaProbeEngine;
class QSqlDatabase;
class ThumbnailEngine;

class MediaTaskService : public QObject {
    Q_OBJECT

public:
    explicit MediaTaskService(DatabaseManager *databaseManager,
                              JobEngine *jobEngine,
                              MediaProbeEngine *mediaProbeEngine,
                              ThumbnailEngine *thumbnailEngine,
                              QObject *parent = nullptr);
    ~MediaTaskService() override;
    void waitForIdle();

public slots:
    void startForSourceRoot(qint64 sourceRootId);
    void recoverStaleThumbnails();

signals:
    void mediaCatalogChanged(const QString &projectDatabasePath);

private:
    enum class PendingWork {
        Metadata,
        Thumbnail
    };

    QVector<AssetFile> fetchAssets(QSqlDatabase &db,
                                   qint64 sourceRootId,
                                   const QList<AssetType> &assetTypes,
                                   PendingWork pendingWork,
                                   QString *errorMessage) const;
    void runMediaJobs(qint64 sourceRootId,
                      const QString &sourceName,
                      const QString &projectDatabasePath,
                      const QString &activeKey,
                      qint64 metadataJobId,
                      qint64 thumbnailJobId);
    bool runMetadataJob(QSqlDatabase &db,
                        const QString &projectDatabasePath,
                        qint64 jobId,
                        const QVector<AssetFile> &assets);
    bool runThumbnailJob(QSqlDatabase &db,
                         const QString &projectDatabasePath,
                         qint64 sourceRootId,
                         qint64 jobId,
                         const QVector<AssetFile> &assets);
    bool markThumbnailRunning(QSqlDatabase &db, qint64 assetId, QString *errorMessage) const;
    bool persistMediaProbe(QSqlDatabase &db, const MediaProbeResult &result, QString *errorMessage) const;
    bool persistThumbnail(QSqlDatabase &db, const ThumbnailResult &result, QString *errorMessage) const;
    void updateJob(const QString &projectDatabasePath,
                   qint64 jobId,
                   qint64 progress,
                   const QString &detail,
                   const JobProgressContext &progressContext = JobProgressContext());
    void completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail);
    void failJob(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage);
    void releaseActiveKey(const QString &activeKey);
    void notifyCatalogChanged(const QString &projectDatabasePath);

    DatabaseManager *m_databaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    MediaProbeEngine *m_mediaProbeEngine = nullptr;
    ThumbnailEngine *m_thumbnailEngine = nullptr;
    QFutureSynchronizer<void> m_futures;
    QMutex m_activeKeysMutex;
    QSet<QString> m_activeKeys;
};
