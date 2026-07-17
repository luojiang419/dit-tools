#pragma once

#include "domain/Entities.h"

#include <QHash>
#include <QObject>
#include <QSet>

class GlobalDatabaseManager;
class AppSettings;
class ImportService;
class LibraryQueryService;
class ProjectService;
class SourceChangeMonitor;
class SystemIdleMonitor;
class VideoAnalysisService;

class BackgroundMaintenanceCoordinator final : public QObject {
    Q_OBJECT

public:
    explicit BackgroundMaintenanceCoordinator(ImportService *importService,
                                              LibraryQueryService *libraryQueryService,
                                               ProjectService *projectService,
                                               GlobalDatabaseManager *globalDatabaseManager,
                                               VideoAnalysisService *videoAnalysisService,
                                               AppSettings *settings,
                                               SourceChangeMonitor *sourceChangeMonitor,
                                              SystemIdleMonitor *systemIdleMonitor,
                                              QObject *parent = nullptr);

    void start();
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isSystemIdle() const;
    [[nodiscard]] int pendingSourceCount() const;
    [[nodiscard]] QString statusText() const;

public slots:
    void applyAnalysisSettings();

signals:
    void stateChanged();

private:
    void handleProjectChanged();
    void reloadSourceRoots(bool markAllDirty);
    void markSourceDirty(qint64 sourceRootId, const QString &sourcePath);
    void processNext();
    void dispatchNextAnalysis();
    void handleSourceScanFinished(qint64 sourceRootId);
    void handleSourceScanFailed(qint64 sourceRootId, const QString &message);
    void setStatusText(const QString &statusText);

    ImportService *m_importService = nullptr;
    LibraryQueryService *m_libraryQueryService = nullptr;
    ProjectService *m_projectService = nullptr;
    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    VideoAnalysisService *m_videoAnalysisService = nullptr;
    AppSettings *m_settings = nullptr;
    SourceChangeMonitor *m_sourceChangeMonitor = nullptr;
    SystemIdleMonitor *m_systemIdleMonitor = nullptr;
    QHash<qint64, SourceRoot> m_sourceRoots;
    QHash<qint64, quint64> m_dirtyGenerations;
    QSet<qint64> m_attemptedThisIdle;
    qint64 m_scanningSourceId = 0;
    quint64 m_scanningGeneration = 0;
    QString m_autoAnalysisVideoKey;
    QString m_statusText;
    bool m_running = false;
    bool m_analysisDispatchBlocked = false;
};
