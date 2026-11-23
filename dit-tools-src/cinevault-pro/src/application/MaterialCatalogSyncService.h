#pragma once

#include "domain/Entities.h"

#include <QFutureSynchronizer>
#include <QMetaType>
#include <QObject>
#include <QQueue>
#include <QVector>
#include <atomic>

class GlobalDatabaseManager;
class IndexingWorkCoordinator;
class JobEngine;
class ProjectService;

enum class CatalogChangeEntity : int {
    Asset = 1,
    Folder = 2
};

enum class CatalogChangeOperation : int {
    Added = 1,
    Updated = 2,
    Removed = 3
};

namespace CatalogChangeMask {
constexpr quint32 AssetCore = 1;
constexpr quint32 AssetMetadata = 2;
constexpr quint32 Thumbnail = 4;
constexpr quint32 Folder = 8;
}

struct CatalogChange {
    qint64 logId = 0;
    CatalogChangeEntity entity = CatalogChangeEntity::Asset;
    CatalogChangeOperation operation = CatalogChangeOperation::Updated;
    qint64 entityId = 0;
    QString entityKey;
    QString previousEntityKey;
    qint64 sourceRootId = 0;
    qint64 previousSourceRootId = 0;
    quint32 changeMask = 0;
};

struct CatalogChangeSet {
    QString projectUuid;
    QVector<CatalogChange> changes;
    qint64 throughLogId = 0;
    bool fullRebuild = false;
};

Q_DECLARE_METATYPE(CatalogChange)
Q_DECLARE_METATYPE(CatalogChangeSet)

class MaterialCatalogSyncService : public QObject {
    Q_OBJECT

public:
    explicit MaterialCatalogSyncService(GlobalDatabaseManager *globalDatabaseManager,
                                        JobEngine *jobEngine,
                                        ProjectService *projectService,
                                        QObject *parent = nullptr);
    ~MaterialCatalogSyncService() override;
    void waitForIdle();
    void setWorkCoordinator(IndexingWorkCoordinator *workCoordinator);

public slots:
    void syncCurrentProject();
    void syncProject(const QString &projectDatabasePath);
    void rebuildAllProjects();

signals:
    void catalogChanged();
    void catalogDeltaReady(const CatalogChangeSet &changeSet);

private:
    void syncProjectRecord(const Project &project);
    void updateJob(const QString &projectDatabasePath,
                   qint64 jobId,
                   qint64 progress,
                   const QString &detail,
                   const JobProgressContext &progressContext = JobProgressContext());
    void completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail);
    void failJob(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage);
    void finishSyncRun();
    void notifyCatalogChanged();
    void notifyCatalogDelta(const CatalogChangeSet &changeSet);

    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    ProjectService *m_projectService = nullptr;
    IndexingWorkCoordinator *m_workCoordinator = nullptr;
    QFutureSynchronizer<void> m_futures;
    QQueue<QString> m_pendingProjectDatabasePaths;
    std::atomic_bool m_syncRunning = false;
};
