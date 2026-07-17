#include "app/AppContext.h"

#include "ui/imaging/LocalImageUrlHelper.h"
#include "ui/window/WindowThemeController.h"

#if CINEVAULT_BUILD_MINIMAL_GUI
#include "ui/viewmodels/MinimalInspectorViewModel.h"
#include "ui/viewmodels/MinimalJobTimelineViewModel.h"
#include "ui/viewmodels/MinimalLibraryWorkspaceViewModel.h"
#include "ui/viewmodels/MinimalReportWorkspaceViewModel.h"
#include "ui/viewmodels/MinimalShellViewModel.h"
#include "ui/viewmodels/MinimalSourceRailViewModel.h"
#else
#include "application/ImportService.h"
#include "application/StorageVolumeService.h"
#include "application/SourceChangeMonitor.h"
#include "application/SystemIdleMonitor.h"
#include "application/BackgroundMaintenanceCoordinator.h"
#include "application/JobService.h"
#include "application/LibraryQueryService.h"
#include "application/FeedbackService.h"
#include "application/DocumentPreviewService.h"
#include "application/MaterialCatalogSyncService.h"
#include "application/MaterialCenterQueryService.h"
#include "application/MediaTaskService.h"
#include "application/MetadataExtractionService.h"
#include "application/ProjectService.h"
#include "application/ReportExportService.h"
#include "application/SearchDocumentSyncService.h"
#include "application/SearchAssistantLifecycleController.h"
#include "application/UpdateService.h"
#include "application/VideoAnalysisService.h"
#include "core/media/MediaProbeEngine.h"
#include "core/jobs/JobEngine.h"
#include "core/scan/ScanEngine.h"
#include "core/search/SearchEngine.h"
#include "core/search/SemanticSearchIndexService.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include "infrastructure/metadata/ExifToolAdapter.h"
#include "infrastructure/network/VisionApiClient.h"
#include "infrastructure/search/LocalSearchAssistantRuntime.h"
#include "infrastructure/search/SearchAssistantClient.h"
#include "ui/viewmodels/InspectorViewModel.h"
#include "ui/viewmodels/JobTimelineViewModel.h"
#include "ui/viewmodels/LibraryWorkspaceViewModel.h"
#include "ui/viewmodels/MaterialCenterViewModel.h"
#include "ui/viewmodels/ProjectLibraryViewModel.h"
#include "ui/viewmodels/ReportWorkspaceViewModel.h"
#include "ui/viewmodels/SettingsViewModel.h"
#include "ui/viewmodels/FeedbackViewModel.h"
#include "ui/viewmodels/ShellViewModel.h"
#include "ui/viewmodels/SourceRailViewModel.h"
#include "ui/window/QuickSearchController.h"
#endif

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>

AppContext::AppContext(QObject *parent)
    : QObject(parent)
    , m_windowThemeController(new WindowThemeController(this))
    , m_localImageUrlHelper(new LocalImageUrlHelper(this))
#if CINEVAULT_BUILD_MINIMAL_GUI
    , m_shellViewModel(new MinimalShellViewModel(this))
    , m_sourceRailViewModel(new MinimalSourceRailViewModel(this))
    , m_libraryWorkspaceViewModel(new MinimalLibraryWorkspaceViewModel(this))
    , m_inspectorViewModel(new MinimalInspectorViewModel(m_sourceRailViewModel, m_libraryWorkspaceViewModel, this))
    , m_jobTimelineViewModel(new MinimalJobTimelineViewModel(this))
    , m_reportWorkspaceViewModel(new MinimalReportWorkspaceViewModel(this))
{
    connect(m_shellViewModel, &MinimalShellViewModel::searchRequested, m_libraryWorkspaceViewModel, &MinimalLibraryWorkspaceViewModel::setSearchText);
    connect(m_sourceRailViewModel, &MinimalSourceRailViewModel::sourceSelected, m_libraryWorkspaceViewModel, &MinimalLibraryWorkspaceViewModel::setSourceFilter);
    connect(m_sourceRailViewModel, &MinimalSourceRailViewModel::sourceSelected, m_inspectorViewModel, &MinimalInspectorViewModel::showSource);
    connect(m_sourceRailViewModel, &MinimalSourceRailViewModel::sourceSelected, m_reportWorkspaceViewModel, &MinimalReportWorkspaceViewModel::setSelectedSource);
    connect(m_libraryWorkspaceViewModel, &MinimalLibraryWorkspaceViewModel::assetSelected, m_inspectorViewModel, &MinimalInspectorViewModel::showAsset);
}

#else
    , m_quickSearchController(new QuickSearchController(&m_settings, this))
    , m_databaseManager(new DatabaseManager(this))
    , m_globalDatabaseManager(new GlobalDatabaseManager(this))
    , m_semanticSearchIndexService(new SemanticSearchIndexService(m_globalDatabaseManager))
    , m_searchDocumentSyncService(new SearchDocumentSyncService(m_globalDatabaseManager, m_semanticSearchIndexService, this))
    , m_searchEngine(new SearchEngine(m_globalDatabaseManager, m_semanticSearchIndexService))
    , m_ffmpegAdapter(new FFmpegAdapter)
    , m_exifToolAdapter(new ExifToolAdapter)
    , m_jobEngine(new JobEngine(m_databaseManager, this))
    , m_mediaProbeEngine(new MediaProbeEngine(m_ffmpegAdapter, this))
    , m_thumbnailEngine(new ThumbnailEngine(m_ffmpegAdapter, &m_settings, this))
    , m_scanEngine(new ScanEngine(m_databaseManager, m_jobEngine, m_mediaProbeEngine, m_thumbnailEngine, this))
    , m_projectService(new ProjectService(m_databaseManager, &m_settings, m_globalDatabaseManager, this))
    , m_jobService(new JobService(m_jobEngine, this))
    , m_mediaTaskService(new MediaTaskService(m_databaseManager, m_jobEngine, m_mediaProbeEngine, m_thumbnailEngine, this))
    , m_metadataExtractionService(new MetadataExtractionService(
          m_databaseManager, m_jobEngine, m_exifToolAdapter, this))
    , m_materialCatalogSyncService(new MaterialCatalogSyncService(m_globalDatabaseManager, m_jobEngine, m_projectService, this))
    , m_materialCenterQueryService(new MaterialCenterQueryService(m_globalDatabaseManager, m_searchEngine, this))
    , m_visionApiClient(new VisionApiClient)
    , m_localSearchAssistantRuntime(new LocalSearchAssistantRuntime({}, {}, this))
    , m_searchAssistantClient(new SearchAssistantClient(this))
    , m_searchAssistantLifecycleController(new SearchAssistantLifecycleController(
          &m_settings, m_localSearchAssistantRuntime, this))
    , m_videoAnalysisService(new VideoAnalysisService(m_globalDatabaseManager, m_jobEngine, &m_settings, m_ffmpegAdapter, m_visionApiClient, this))
    , m_feedbackService(new FeedbackService(&m_settings, m_projectService, this))
    , m_updateService(new UpdateService(&m_settings, this))
    , m_importService(new ImportService(m_databaseManager, m_jobService, m_scanEngine, this))
    , m_storageVolumeService(new StorageVolumeService(this))
    , m_libraryQueryService(new LibraryQueryService(m_databaseManager, m_searchEngine, this))
    , m_sourceChangeMonitor(new SourceChangeMonitor(this))
    , m_systemIdleMonitor(new SystemIdleMonitor(this))
    , m_backgroundMaintenanceCoordinator(new BackgroundMaintenanceCoordinator(
          m_importService,
          m_libraryQueryService,
          m_projectService,
          m_globalDatabaseManager,
          m_videoAnalysisService,
          &m_settings,
          m_sourceChangeMonitor,
          m_systemIdleMonitor,
          this))
    , m_documentPreviewService(new DocumentPreviewService(this))
    , m_reportExportService(new ReportExportService(m_databaseManager, m_projectService, this))
    , m_shellViewModel(new ShellViewModel(m_projectService,
                                         m_importService,
                                         m_feedbackService,
                                         m_storageVolumeService,
                                         this))
    , m_projectLibraryViewModel(new ProjectLibraryViewModel(m_projectService, this))
    , m_sourceRailViewModel(new SourceRailViewModel(m_libraryQueryService, this))
    , m_libraryWorkspaceViewModel(new LibraryWorkspaceViewModel(m_libraryQueryService, this))
    , m_materialCenterViewModel(new MaterialCenterViewModel(m_materialCenterQueryService, m_materialCatalogSyncService, m_searchDocumentSyncService, m_videoAnalysisService, m_projectService, &m_settings, m_localSearchAssistantRuntime, m_searchAssistantClient, this))
    , m_inspectorViewModel(new InspectorViewModel(m_libraryQueryService, this))
    , m_jobTimelineViewModel(new JobTimelineViewModel(m_jobService, m_videoAnalysisService, this))
    , m_reportWorkspaceViewModel(new ReportWorkspaceViewModel(m_projectService, m_libraryQueryService, m_reportExportService, this))
    , m_settingsViewModel(new SettingsViewModel(&m_settings,
                                                m_visionApiClient,
                                                m_videoAnalysisService,
                                                m_updateService,
                                                m_quickSearchController,
                                                m_localSearchAssistantRuntime,
                                                m_searchAssistantLifecycleController,
                                                this))
    , m_feedbackViewModel(new FeedbackViewModel(m_feedbackService, this))
{
    QString globalDbError;
    m_globalDatabaseManager->openDatabase(&globalDbError);
    if (m_globalDatabaseManager->isOpen()) {
        m_searchDocumentSyncService->scheduleFullSync();
    }

    connect(m_projectService, &ProjectService::projectChanged, m_shellViewModel, &ShellViewModel::resetProjectUiState);
    connect(m_projectService, &ProjectService::projectChanged, m_jobEngine, &JobEngine::reloadJobs);
    connect(m_projectService, &ProjectService::projectChanged, m_sourceRailViewModel, &SourceRailViewModel::resetForProject);
    connect(m_projectService, &ProjectService::projectChanged, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::resetForProject);
    connect(m_projectService, &ProjectService::projectChanged, m_jobTimelineViewModel, &JobTimelineViewModel::reload);
    connect(m_projectService, &ProjectService::projectChanged, m_inspectorViewModel, &InspectorViewModel::clear);
    connect(m_projectService, &ProjectService::projectChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject, Qt::QueuedConnection);
    connect(m_projectService, &ProjectService::projectChanged, m_mediaTaskService, &MediaTaskService::recoverStaleThumbnails);
    connect(m_projectService, &ProjectService::projectChanged, m_importService, &ImportService::resumeInterruptedScans);
    connect(m_projectService, &ProjectService::projectChanged, m_importService, &ImportService::rescanLegacySourceRoots);
    connect(m_importService, &ImportService::catalogChanged, m_sourceRailViewModel, &SourceRailViewModel::reload, Qt::QueuedConnection);
    connect(m_importService, &ImportService::catalogChanged, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::reload, Qt::QueuedConnection);
    connect(m_importService, &ImportService::catalogChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject, Qt::QueuedConnection);

    connect(m_scanEngine, &ScanEngine::scanFinished, m_mediaTaskService, &MediaTaskService::startForSourceRoot);
    connect(m_scanEngine,
            &ScanEngine::scanFinished,
            m_metadataExtractionService,
            &MetadataExtractionService::startForSourceRoot);
    connect(m_mediaTaskService, &MediaTaskService::mediaCatalogChanged, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::reload, Qt::QueuedConnection);
    connect(m_mediaTaskService, &MediaTaskService::mediaCatalogChanged, m_inspectorViewModel, &InspectorViewModel::reload, Qt::QueuedConnection);
    connect(m_mediaTaskService, &MediaTaskService::mediaCatalogChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncProject, Qt::QueuedConnection);
    connect(m_metadataExtractionService,
            &MetadataExtractionService::metadataCatalogChanged,
            m_libraryWorkspaceViewModel,
            &LibraryWorkspaceViewModel::reload,
            Qt::QueuedConnection);
    connect(m_metadataExtractionService,
            &MetadataExtractionService::metadataCatalogChanged,
            m_inspectorViewModel,
            &InspectorViewModel::reload,
            Qt::QueuedConnection);
    connect(m_metadataExtractionService,
            &MetadataExtractionService::metadataCatalogChanged,
            m_materialCatalogSyncService,
            &MaterialCatalogSyncService::syncProject,
            Qt::QueuedConnection);
    connect(m_libraryQueryService, &LibraryQueryService::dataChanged, m_sourceRailViewModel, &SourceRailViewModel::reload, Qt::QueuedConnection);
    connect(m_libraryQueryService, &LibraryQueryService::dataChanged, m_inspectorViewModel, &InspectorViewModel::reload, Qt::QueuedConnection);
    connect(m_libraryQueryService, &LibraryQueryService::dataChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject, Qt::QueuedConnection);
    connect(m_materialCatalogSyncService, &MaterialCatalogSyncService::catalogChanged,
            m_searchDocumentSyncService, &SearchDocumentSyncService::scheduleFullSync);
    connect(m_videoAnalysisService, &VideoAnalysisService::catalogChanged,
            m_searchDocumentSyncService, &SearchDocumentSyncService::scheduleFullSync);
    connect(m_settingsViewModel, &SettingsViewModel::searchSettingsChanged,
            m_materialCenterViewModel, &MaterialCenterViewModel::reload);
    connect(m_settingsViewModel,
            &SettingsViewModel::searchSettingsChanged,
            m_backgroundMaintenanceCoordinator,
            &BackgroundMaintenanceCoordinator::applyAnalysisSettings);
    connect(m_materialCenterViewModel,
            &MaterialCenterViewModel::quickSearchNavigationRequested,
            m_shellViewModel,
            &ShellViewModel::enterMaterialCenterFromQuickSearch);
    connect(m_materialCenterViewModel,
            &MaterialCenterViewModel::searchAssistantWarmupRequested,
            m_searchAssistantLifecycleController,
            &SearchAssistantLifecycleController::recordSearchIntent);
    connect(m_quickSearchController,
            &QuickSearchController::quickSearchRequested,
            m_searchAssistantLifecycleController,
            &SearchAssistantLifecycleController::recordSearchIntent);
    connect(m_quickSearchController,
            &QuickSearchController::showMainWindowRequested,
            m_searchAssistantLifecycleController,
            &SearchAssistantLifecycleController::recordUserActivity);

    connect(m_shellViewModel, &ShellViewModel::searchRequested, this, [this](const QString &text) {
        if (m_shellViewModel->currentWorkspace() == static_cast<int>(WorkspaceId::ProjectLibrary)) {
            m_projectLibraryViewModel->setSearchText(text);
        } else if (m_shellViewModel->currentWorkspace() == static_cast<int>(WorkspaceId::MaterialCenter)) {
            m_materialCenterViewModel->setSearchText(text);
        } else if (m_shellViewModel->currentWorkspace() == static_cast<int>(WorkspaceId::Feedback)) {
            return;
        } else {
            m_libraryWorkspaceViewModel->setSearchText(text);
        }
    });
    connect(m_projectLibraryViewModel, &ProjectLibraryViewModel::projectActivated, this, [this]() {
        m_shellViewModel->enterProjectFromLibrary();
    });
    connect(m_sourceRailViewModel, &SourceRailViewModel::sourceSelected, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::setSourceFilter);
    connect(m_sourceRailViewModel, &SourceRailViewModel::sourceSelected, m_inspectorViewModel, &InspectorViewModel::showSource);
    connect(m_sourceRailViewModel, &SourceRailViewModel::sourceSelected, m_reportWorkspaceViewModel, &ReportWorkspaceViewModel::setSelectedSource);
    connect(m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::assetSelected, m_inspectorViewModel, &InspectorViewModel::showAsset);
}
#endif

AppContext::~AppContext()
{
#if !CINEVAULT_BUILD_MINIMAL_GUI
    if (m_settingsViewModel) {
        m_settingsViewModel->waitForIdle();
    }
    if (m_videoAnalysisService) {
        m_videoAnalysisService->waitForIdle();
    }
    if (m_materialCatalogSyncService) {
        m_materialCatalogSyncService->waitForIdle();
    }
    if (m_metadataExtractionService) {
        m_metadataExtractionService->waitForIdle();
    }
    if (m_mediaTaskService) {
        m_mediaTaskService->waitForIdle();
    }
    if (m_scanEngine) {
        m_scanEngine->waitForIdle();
    }
#endif
}

void AppContext::expose(QQmlApplicationEngine &engine)
{
    auto *context = engine.rootContext();
    context->setContextProperty(QStringLiteral("shellVm"), m_shellViewModel);
    context->setContextProperty(QStringLiteral("windowThemeController"), m_windowThemeController);
    context->setContextProperty(QStringLiteral("localImageUrlHelper"), m_localImageUrlHelper);
    context->setContextProperty(QStringLiteral("sourceRailVm"), m_sourceRailViewModel);
    context->setContextProperty(QStringLiteral("libraryWorkspaceVm"), m_libraryWorkspaceViewModel);
    context->setContextProperty(QStringLiteral("inspectorVm"), m_inspectorViewModel);
    context->setContextProperty(QStringLiteral("jobTimelineVm"), m_jobTimelineViewModel);
    context->setContextProperty(QStringLiteral("reportWorkspaceVm"), m_reportWorkspaceViewModel);
#if CINEVAULT_BUILD_MINIMAL_GUI
    context->setContextProperty(QStringLiteral("quickSearchController"), QVariant());
    context->setContextProperty(QStringLiteral("projectLibraryVm"), QVariant());
    context->setContextProperty(QStringLiteral("materialCenterVm"), QVariant());
    context->setContextProperty(QStringLiteral("documentPreviewVm"), QVariant());
    context->setContextProperty(QStringLiteral("settingsVm"), QVariant());
    context->setContextProperty(QStringLiteral("feedbackVm"), QVariant());
#else
    context->setContextProperty(QStringLiteral("quickSearchController"), m_quickSearchController);
    context->setContextProperty(QStringLiteral("projectLibraryVm"), m_projectLibraryViewModel);
    context->setContextProperty(QStringLiteral("materialCenterVm"), m_materialCenterViewModel);
    context->setContextProperty(QStringLiteral("documentPreviewVm"), m_documentPreviewService);
    context->setContextProperty(QStringLiteral("settingsVm"), m_settingsViewModel);
    context->setContextProperty(QStringLiteral("feedbackVm"), m_feedbackViewModel);
#endif
}

void AppContext::startInteractiveServices()
{
#if !CINEVAULT_BUILD_MINIMAL_GUI
    if (m_searchAssistantLifecycleController) {
        m_searchAssistantLifecycleController->start();
        if (QCoreApplication::arguments().contains(
                QStringLiteral("--search-assistant-startup-probe"),
                Qt::CaseInsensitive)) {
            m_searchAssistantLifecycleController->recordSearchIntent();
        }
    }
    if (m_backgroundMaintenanceCoordinator) {
        m_backgroundMaintenanceCoordinator->start();
    }
#endif
}

#if !CINEVAULT_BUILD_MINIMAL_GUI
bool AppContext::startAnalysisProbe(const QString &projectPath,
                                    const QString &videoKey,
                                    QString *errorMessage)
{
    const auto normalizedProjectPath = projectPath.trimmed();
    const auto normalizedVideoKey = videoKey.trimmed();
    if (normalizedProjectPath.isEmpty() || normalizedVideoKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("端到端解析测试缺少项目路径或素材键");
        }
        return false;
    }
    if (!m_projectService || !m_materialCatalogSyncService || !m_videoAnalysisService) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("端到端解析测试所需服务未初始化");
        }
        return false;
    }

    m_analysisProbeVideoKey = normalizedVideoKey;
    m_analysisProbeEnqueued = false;
    m_analysisProbeFinished = false;

    connect(m_videoAnalysisService,
            &VideoAnalysisService::analysisProgressChanged,
            this,
            [this](const QString &reportedVideoKey,
                   qint64 progress,
                   const QString &detail,
                   int state,
                   const QString &reportedError) {
                if (reportedVideoKey != m_analysisProbeVideoKey || m_analysisProbeFinished) {
                    return;
                }
                emit analysisProbeProgress(progress, detail, state, reportedError);
                if (state == static_cast<int>(JobState::Completed)) {
                    m_analysisProbeFinished = true;
                    emit analysisProbeFinished(true, detail);
                } else if (state == static_cast<int>(JobState::Failed)) {
                    m_analysisProbeFinished = true;
                    emit analysisProbeFinished(false,
                                               reportedError.trimmed().isEmpty() ? detail : reportedError);
                }
            });

    connect(m_materialCatalogSyncService,
            &MaterialCatalogSyncService::catalogChanged,
            this,
            [this]() {
                if (m_analysisProbeEnqueued || m_analysisProbeFinished) {
                    return;
                }
                m_analysisProbeEnqueued = true;
                QString enqueueError;
                if (!m_videoAnalysisService->enqueueVideo(m_analysisProbeVideoKey, &enqueueError)) {
                    m_analysisProbeFinished = true;
                    emit analysisProbeFinished(false, enqueueError);
                }
            });

    if (!m_projectService->openProject(normalizedProjectPath, errorMessage)) {
        return false;
    }
    return true;
}
#endif
