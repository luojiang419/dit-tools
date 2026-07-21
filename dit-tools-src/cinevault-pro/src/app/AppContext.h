#pragma once

#include <QObject>
#include <QVariant>

class QQmlApplicationEngine;
class WindowThemeController;
class LocalImageUrlHelper;
#if CINEVAULT_BUILD_MINIMAL_GUI
class MinimalShellViewModel;
class MinimalSourceRailViewModel;
class MinimalLibraryWorkspaceViewModel;
class MinimalInspectorViewModel;
class MinimalJobTimelineViewModel;
class MinimalReportWorkspaceViewModel;
#else
#include "infrastructure/config/AppSettings.h"
class DatabaseManager;
class GlobalDatabaseManager;
class IndexingWorkCoordinator;
class SearchEngine;
class SemanticSearchIndexService;
class SearchDocumentSyncService;
class FFmpegAdapter;
class ExifToolAdapter;
class JobEngine;
class MediaProbeEngine;
class ScanEngine;
class ThumbnailEngine;
class ProjectService;
class JobService;
class MediaTaskService;
class MetadataExtractionService;
class MaterialCatalogSyncService;
class MaterialCenterQueryService;
class VideoAnalysisService;
class FeedbackService;
class UpdateService;
class ImportService;
class StorageVolumeService;
class SourceChangeMonitor;
class SystemIdleMonitor;
class BackgroundMaintenanceCoordinator;
class LibraryQueryService;
class DocumentPreviewService;
class ReportExportService;
class VisionApiClient;
class LocalSearchAssistantRuntime;
class SearchAssistantClient;
class SearchAssistantLifecycleController;
class ShellViewModel;
class ProjectLibraryViewModel;
class SourceRailViewModel;
class LibraryWorkspaceViewModel;
class MaterialCenterViewModel;
class InspectorViewModel;
class JobTimelineViewModel;
class ReportWorkspaceViewModel;
class SettingsViewModel;
class FeedbackViewModel;
class QuickSearchController;
class UiHeartbeatMonitor;
#endif

class AppContext : public QObject {
    Q_OBJECT

public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    void expose(QQmlApplicationEngine &engine);
    void startInteractiveServices();

#if !CINEVAULT_BUILD_MINIMAL_GUI
    bool startAnalysisProbe(const QString &projectPath,
                            const QString &videoKey,
                            QString *errorMessage = nullptr);

signals:
    void analysisProbeProgress(qint64 progress,
                               const QString &detail,
                               int state,
                               const QString &errorMessage);
    void analysisProbeFinished(bool success, const QString &message);
#endif

private:
    WindowThemeController *m_windowThemeController = nullptr;
    LocalImageUrlHelper *m_localImageUrlHelper = nullptr;

#if CINEVAULT_BUILD_MINIMAL_GUI
    MinimalShellViewModel *m_shellViewModel = nullptr;
    MinimalSourceRailViewModel *m_sourceRailViewModel = nullptr;
    MinimalLibraryWorkspaceViewModel *m_libraryWorkspaceViewModel = nullptr;
    MinimalInspectorViewModel *m_inspectorViewModel = nullptr;
    MinimalJobTimelineViewModel *m_jobTimelineViewModel = nullptr;
    MinimalReportWorkspaceViewModel *m_reportWorkspaceViewModel = nullptr;
#else
    AppSettings m_settings;
    UiHeartbeatMonitor *m_uiHeartbeatMonitor = nullptr;
    QuickSearchController *m_quickSearchController = nullptr;
    DatabaseManager *m_databaseManager = nullptr;
    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    IndexingWorkCoordinator *m_indexingWorkCoordinator = nullptr;
    SemanticSearchIndexService *m_semanticSearchIndexService = nullptr;
    SearchDocumentSyncService *m_searchDocumentSyncService = nullptr;
    SearchEngine *m_searchEngine = nullptr;
    FFmpegAdapter *m_ffmpegAdapter = nullptr;
    ExifToolAdapter *m_exifToolAdapter = nullptr;
    JobEngine *m_jobEngine = nullptr;
    MediaProbeEngine *m_mediaProbeEngine = nullptr;
    ThumbnailEngine *m_thumbnailEngine = nullptr;
    ScanEngine *m_scanEngine = nullptr;
    ProjectService *m_projectService = nullptr;
    JobService *m_jobService = nullptr;
    MediaTaskService *m_mediaTaskService = nullptr;
    MetadataExtractionService *m_metadataExtractionService = nullptr;
    MaterialCatalogSyncService *m_materialCatalogSyncService = nullptr;
    MaterialCenterQueryService *m_materialCenterQueryService = nullptr;
    VisionApiClient *m_visionApiClient = nullptr;
    LocalSearchAssistantRuntime *m_localSearchAssistantRuntime = nullptr;
    SearchAssistantClient *m_searchAssistantClient = nullptr;
    SearchAssistantLifecycleController *m_searchAssistantLifecycleController = nullptr;
    VideoAnalysisService *m_videoAnalysisService = nullptr;
    FeedbackService *m_feedbackService = nullptr;
    UpdateService *m_updateService = nullptr;
    ImportService *m_importService = nullptr;
    StorageVolumeService *m_storageVolumeService = nullptr;
    LibraryQueryService *m_libraryQueryService = nullptr;
    SourceChangeMonitor *m_sourceChangeMonitor = nullptr;
    SystemIdleMonitor *m_systemIdleMonitor = nullptr;
    BackgroundMaintenanceCoordinator *m_backgroundMaintenanceCoordinator = nullptr;
    DocumentPreviewService *m_documentPreviewService = nullptr;
    ReportExportService *m_reportExportService = nullptr;
    ShellViewModel *m_shellViewModel = nullptr;
    ProjectLibraryViewModel *m_projectLibraryViewModel = nullptr;
    SourceRailViewModel *m_sourceRailViewModel = nullptr;
    LibraryWorkspaceViewModel *m_libraryWorkspaceViewModel = nullptr;
    MaterialCenterViewModel *m_materialCenterViewModel = nullptr;
    InspectorViewModel *m_inspectorViewModel = nullptr;
    JobTimelineViewModel *m_jobTimelineViewModel = nullptr;
    ReportWorkspaceViewModel *m_reportWorkspaceViewModel = nullptr;
    SettingsViewModel *m_settingsViewModel = nullptr;
    FeedbackViewModel *m_feedbackViewModel = nullptr;
    QString m_analysisProbeVideoKey;
    bool m_analysisProbeEnqueued = false;
    bool m_analysisProbeFinished = false;
#endif
};
