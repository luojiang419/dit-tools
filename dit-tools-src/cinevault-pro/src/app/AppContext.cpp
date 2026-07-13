#include "app/AppContext.h"

#include "ui/imaging/LocalImageUrlHelper.h"
#include "ui/window/WindowThemeController.h"

#if CINEVAULT_BUILD_MINIMAL_GUI
#include "ui/viewmodels/MinimalImportWorkspaceViewModel.h"
#include "ui/viewmodels/MinimalInspectorViewModel.h"
#include "ui/viewmodels/MinimalJobTimelineViewModel.h"
#include "ui/viewmodels/MinimalLibraryWorkspaceViewModel.h"
#include "ui/viewmodels/MinimalMaterialBackupViewModel.h"
#include "ui/viewmodels/MinimalReportWorkspaceViewModel.h"
#include "ui/viewmodels/MinimalShellViewModel.h"
#include "ui/viewmodels/MinimalSourceRailViewModel.h"
#else
#include "application/ImportService.h"
#include "application/JobService.h"
#include "application/LibraryQueryService.h"
#include "application/FeedbackService.h"
#include "application/DocumentPreviewService.h"
#include "application/MaterialBackupService.h"
#include "application/MaterialCatalogSyncService.h"
#include "application/MaterialCenterQueryService.h"
#include "application/MediaTaskService.h"
#include "application/ProjectService.h"
#include "application/ReportExportService.h"
#include "application/UpdateService.h"
#include "application/VideoAnalysisService.h"
#include "core/media/MediaProbeEngine.h"
#include "core/jobs/JobEngine.h"
#include "core/scan/ScanEngine.h"
#include "core/search/SearchEngine.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include "infrastructure/network/VisionApiClient.h"
#include "ui/viewmodels/ImportWorkspaceViewModel.h"
#include "ui/viewmodels/InspectorViewModel.h"
#include "ui/viewmodels/JobTimelineViewModel.h"
#include "ui/viewmodels/LibraryWorkspaceViewModel.h"
#include "ui/viewmodels/MaterialBackupViewModel.h"
#include "ui/viewmodels/MaterialCenterViewModel.h"
#include "ui/viewmodels/ProjectLibraryViewModel.h"
#include "ui/viewmodels/ReportWorkspaceViewModel.h"
#include "ui/viewmodels/SettingsViewModel.h"
#include "ui/viewmodels/FeedbackViewModel.h"
#include "ui/viewmodels/ShellViewModel.h"
#include "ui/viewmodels/SourceRailViewModel.h"
#endif

#include <QQmlApplicationEngine>
#include <QQmlContext>

AppContext::AppContext(QObject *parent)
    : QObject(parent)
    , m_windowThemeController(new WindowThemeController(this))
    , m_localImageUrlHelper(new LocalImageUrlHelper(this))
#if CINEVAULT_BUILD_MINIMAL_GUI
    , m_shellViewModel(new MinimalShellViewModel(this))
    , m_sourceRailViewModel(new MinimalSourceRailViewModel(this))
    , m_importWorkspaceViewModel(new MinimalImportWorkspaceViewModel(this))
    , m_materialBackupViewModel(new MinimalMaterialBackupViewModel(this))
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
    , m_databaseManager(new DatabaseManager(this))
    , m_globalDatabaseManager(new GlobalDatabaseManager(this))
    , m_searchEngine(new SearchEngine)
    , m_ffmpegAdapter(new FFmpegAdapter)
    , m_jobEngine(new JobEngine(m_databaseManager, this))
    , m_mediaProbeEngine(new MediaProbeEngine(m_ffmpegAdapter, this))
    , m_thumbnailEngine(new ThumbnailEngine(m_ffmpegAdapter, &m_settings, this))
    , m_scanEngine(new ScanEngine(m_databaseManager, m_jobEngine, m_mediaProbeEngine, m_thumbnailEngine, this))
    , m_projectService(new ProjectService(m_databaseManager, &m_settings, m_globalDatabaseManager, this))
    , m_jobService(new JobService(m_jobEngine, this))
    , m_mediaTaskService(new MediaTaskService(m_databaseManager, m_jobEngine, m_mediaProbeEngine, m_thumbnailEngine, this))
    , m_importService(new ImportService(m_databaseManager, m_jobService, m_scanEngine, this))
    , m_libraryQueryService(new LibraryQueryService(m_databaseManager, m_searchEngine, this))
    , m_documentPreviewService(new DocumentPreviewService(this))
    , m_reportExportService(new ReportExportService(m_databaseManager, m_projectService, this))
    , m_materialCatalogSyncService(new MaterialCatalogSyncService(m_globalDatabaseManager, m_jobEngine, m_projectService, this))
    , m_materialCenterQueryService(new MaterialCenterQueryService(m_globalDatabaseManager, m_searchEngine, this))
    , m_materialBackupService(new MaterialBackupService(m_jobEngine, this))
    , m_visionApiClient(new VisionApiClient)
    , m_videoAnalysisService(new VideoAnalysisService(m_globalDatabaseManager, m_jobEngine, &m_settings, m_ffmpegAdapter, m_visionApiClient, this))
    , m_feedbackService(new FeedbackService(&m_settings, m_projectService, this))
    , m_updateService(new UpdateService(&m_settings, this))
    , m_shellViewModel(new ShellViewModel(m_projectService, m_importService, m_feedbackService, this))
    , m_projectLibraryViewModel(new ProjectLibraryViewModel(m_projectService, this))
    , m_sourceRailViewModel(new SourceRailViewModel(m_libraryQueryService, this))
    , m_importWorkspaceViewModel(new ImportWorkspaceViewModel(m_importService, this))
    , m_materialBackupViewModel(new MaterialBackupViewModel(m_projectService, m_materialBackupService, m_importService, &m_settings, this))
    , m_libraryWorkspaceViewModel(new LibraryWorkspaceViewModel(m_libraryQueryService, this))
    , m_materialCenterViewModel(new MaterialCenterViewModel(m_materialCenterQueryService, m_materialCatalogSyncService, m_videoAnalysisService, m_projectService, &m_settings, this))
    , m_inspectorViewModel(new InspectorViewModel(m_libraryQueryService, this))
    , m_jobTimelineViewModel(new JobTimelineViewModel(m_jobService, m_videoAnalysisService, this))
    , m_reportWorkspaceViewModel(new ReportWorkspaceViewModel(m_projectService, m_libraryQueryService, m_reportExportService, this))
    , m_settingsViewModel(new SettingsViewModel(&m_settings, m_visionApiClient, m_videoAnalysisService, m_updateService, this))
    , m_feedbackViewModel(new FeedbackViewModel(m_feedbackService, this))
{
    QString globalDbError;
    m_globalDatabaseManager->openDatabase(&globalDbError);

    connect(m_projectService, &ProjectService::projectChanged, m_shellViewModel, &ShellViewModel::resetProjectUiState);
    connect(m_projectService, &ProjectService::projectChanged, m_jobEngine, &JobEngine::clearJobs);
    connect(m_projectService, &ProjectService::projectChanged, m_sourceRailViewModel, &SourceRailViewModel::resetForProject);
    connect(m_projectService, &ProjectService::projectChanged, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::resetForProject);
    connect(m_projectService, &ProjectService::projectChanged, m_jobTimelineViewModel, &JobTimelineViewModel::reload);
    connect(m_projectService, &ProjectService::projectChanged, m_inspectorViewModel, &InspectorViewModel::clear);
    connect(m_projectService, &ProjectService::projectChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject);
    connect(m_projectService, &ProjectService::projectChanged, m_mediaTaskService, &MediaTaskService::recoverStaleThumbnails);
    connect(m_projectService, &ProjectService::projectChanged, m_importService, &ImportService::rescanLegacySourceRoots);
    connect(m_importService, &ImportService::catalogChanged, m_sourceRailViewModel, &SourceRailViewModel::reload);
    connect(m_importService, &ImportService::catalogChanged, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::reload);
    connect(m_importService, &ImportService::catalogChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject);

    connect(m_scanEngine, &ScanEngine::scanFinished, m_mediaTaskService, &MediaTaskService::startForSourceRoot);
    connect(m_mediaTaskService, &MediaTaskService::mediaCatalogChanged, m_libraryWorkspaceViewModel, &LibraryWorkspaceViewModel::reload);
    connect(m_mediaTaskService, &MediaTaskService::mediaCatalogChanged, m_inspectorViewModel, &InspectorViewModel::reload);
    connect(m_mediaTaskService, &MediaTaskService::mediaCatalogChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject);
    connect(m_libraryQueryService, &LibraryQueryService::dataChanged, m_sourceRailViewModel, &SourceRailViewModel::reload);
    connect(m_libraryQueryService, &LibraryQueryService::dataChanged, m_inspectorViewModel, &InspectorViewModel::reload);
    connect(m_libraryQueryService, &LibraryQueryService::dataChanged, m_materialCatalogSyncService, &MaterialCatalogSyncService::syncCurrentProject);

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

void AppContext::expose(QQmlApplicationEngine &engine)
{
    auto *context = engine.rootContext();
    context->setContextProperty(QStringLiteral("shellVm"), m_shellViewModel);
    context->setContextProperty(QStringLiteral("windowThemeController"), m_windowThemeController);
    context->setContextProperty(QStringLiteral("localImageUrlHelper"), m_localImageUrlHelper);
    context->setContextProperty(QStringLiteral("sourceRailVm"), m_sourceRailViewModel);
    context->setContextProperty(QStringLiteral("importWorkspaceVm"), m_importWorkspaceViewModel);
    context->setContextProperty(QStringLiteral("materialBackupVm"), m_materialBackupViewModel);
    context->setContextProperty(QStringLiteral("libraryWorkspaceVm"), m_libraryWorkspaceViewModel);
    context->setContextProperty(QStringLiteral("inspectorVm"), m_inspectorViewModel);
    context->setContextProperty(QStringLiteral("jobTimelineVm"), m_jobTimelineViewModel);
    context->setContextProperty(QStringLiteral("reportWorkspaceVm"), m_reportWorkspaceViewModel);
#if CINEVAULT_BUILD_MINIMAL_GUI
    context->setContextProperty(QStringLiteral("projectLibraryVm"), QVariant());
    context->setContextProperty(QStringLiteral("materialCenterVm"), QVariant());
    context->setContextProperty(QStringLiteral("documentPreviewVm"), QVariant());
    context->setContextProperty(QStringLiteral("settingsVm"), QVariant());
    context->setContextProperty(QStringLiteral("feedbackVm"), QVariant());
#else
    context->setContextProperty(QStringLiteral("projectLibraryVm"), m_projectLibraryViewModel);
    context->setContextProperty(QStringLiteral("materialCenterVm"), m_materialCenterViewModel);
    context->setContextProperty(QStringLiteral("documentPreviewVm"), m_documentPreviewService);
    context->setContextProperty(QStringLiteral("settingsVm"), m_settingsViewModel);
    context->setContextProperty(QStringLiteral("feedbackVm"), m_feedbackViewModel);
#endif
}
