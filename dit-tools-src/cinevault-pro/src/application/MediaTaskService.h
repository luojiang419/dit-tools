#pragma once

#include "domain/Entities.h"

#include <QObject>
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

public slots:
    void startForSourceRoot(qint64 sourceRootId);
    void recoverStaleThumbnails();

signals:
    void mediaCatalogChanged();

private:
    QVector<AssetFile> fetchAssets(QSqlDatabase &db, qint64 sourceRootId, const QList<AssetType> &assetTypes, QString *errorMessage) const;
    void runMediaJobs(qint64 sourceRootId,
                      const QString &sourceName,
                      const QString &projectDatabasePath,
                      qint64 metadataJobId,
                      qint64 thumbnailJobId);
    bool runMetadataJob(QSqlDatabase &db, qint64 jobId, const QVector<AssetFile> &assets);
    bool runThumbnailJob(QSqlDatabase &db,
                         const QString &projectDatabasePath,
                         qint64 sourceRootId,
                         qint64 jobId,
                         const QVector<AssetFile> &assets);
    QVector<AssetFile> fetchStaleThumbnailAssets(QSqlDatabase &db, qint64 sourceRootId, QString *errorMessage) const;
    void runStaleThumbnailRecovery(qint64 sourceRootId,
                                   const QString &sourceName,
                                   const QString &projectDatabasePath,
                                   qint64 thumbnailJobId);
    bool markThumbnailRunning(QSqlDatabase &db, qint64 assetId, QString *errorMessage) const;
    bool persistMediaProbe(QSqlDatabase &db, const MediaProbeResult &result, QString *errorMessage) const;
    bool persistThumbnail(QSqlDatabase &db, const ThumbnailResult &result, QString *errorMessage) const;
    void updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext = JobProgressContext());
    void completeJob(qint64 jobId, const QString &detail);
    void failJob(qint64 jobId, const QString &errorMessage);
    void notifyCatalogChanged();

    DatabaseManager *m_databaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    MediaProbeEngine *m_mediaProbeEngine = nullptr;
    ThumbnailEngine *m_thumbnailEngine = nullptr;
};
