#pragma once

#include "domain/Entities.h"

#include <QFutureSynchronizer>
#include <QObject>
#include <QQueue>
#include <atomic>

class GlobalDatabaseManager;
class JobEngine;
class ProjectService;

class MaterialCatalogSyncService : public QObject {
    Q_OBJECT

public:
    explicit MaterialCatalogSyncService(GlobalDatabaseManager *globalDatabaseManager,
                                        JobEngine *jobEngine,
                                        ProjectService *projectService,
                                        QObject *parent = nullptr);
    ~MaterialCatalogSyncService() override;
    void waitForIdle();

public slots:
    void syncCurrentProject();
    void syncProject(const QString &projectDatabasePath);
    void rebuildAllProjects();

signals:
    void catalogChanged();

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

    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    ProjectService *m_projectService = nullptr;
    QFutureSynchronizer<void> m_futures;
    QQueue<QString> m_pendingProjectDatabasePaths;
    std::atomic_bool m_syncRunning = false;
};
