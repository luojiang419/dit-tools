#pragma once

#include "domain/Entities.h"
#include "infrastructure/metadata/ExifToolAdapter.h"

#include <QFutureSynchronizer>
#include <QObject>
#include <QSet>

class DatabaseManager;
class JobEngine;
class QSqlDatabase;

class MetadataExtractionService final : public QObject {
    Q_OBJECT

public:
    explicit MetadataExtractionService(DatabaseManager *databaseManager,
                                       JobEngine *jobEngine,
                                       ExifToolAdapter *exifToolAdapter,
                                       QObject *parent = nullptr);
    ~MetadataExtractionService() override;
    void waitForIdle();

public slots:
    void startForSourceRoot(qint64 sourceRootId);

signals:
    void metadataCatalogChanged(const QString &projectDatabasePath);

private:
    QVector<AssetFile> fetchPendingAssets(QSqlDatabase &db,
                                          qint64 sourceRootId,
                                          QString *errorMessage) const;
    void runExtraction(qint64 sourceRootId,
                       const QString &projectDatabasePath,
                       const QString &activeKey,
                       qint64 jobId);
    bool persistBatch(QSqlDatabase &db,
                      const QVector<EmbeddedMetadataResult> &results,
                      QString *errorMessage) const;
    void updateJob(const QString &projectDatabasePath,
                   qint64 jobId,
                   qint64 progress,
                   const QString &detail,
                   const JobProgressContext &context);
    void completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail);
    void failJob(const QString &projectDatabasePath, qint64 jobId, const QString &message);
    void releaseActiveKey(const QString &activeKey);

    DatabaseManager *m_databaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    ExifToolAdapter *m_exifToolAdapter = nullptr;
    QFutureSynchronizer<void> m_futures;
    QSet<QString> m_activeKeys;
};
