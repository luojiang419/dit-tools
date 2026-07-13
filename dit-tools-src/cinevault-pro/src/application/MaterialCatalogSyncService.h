#pragma once

#include "domain/Entities.h"

#include <QObject>
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

public slots:
    void syncCurrentProject();
    void rebuildAllProjects();

signals:
    void catalogChanged();

private:
    void updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext = JobProgressContext());
    void completeJob(qint64 jobId, const QString &detail);
    void failJob(qint64 jobId, const QString &errorMessage);
    void finishSyncRun();
    void notifyCatalogChanged();

    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    ProjectService *m_projectService = nullptr;
    std::atomic_bool m_syncRunning = false;
    std::atomic_bool m_syncPending = false;
};
