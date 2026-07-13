cmake_minimum_required(VERSION 3.24)

project(CineVault VERSION 0.1.97 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(CINEVAULT_BUILD_MINIMAL_GUI "Build the milestone A minimal GUI shell" ON)
option(CINEVAULT_ENABLE_FFMPEG "Enable FFmpeg-backed media modules" OFF)
set(QT_ROOT "" CACHE PATH "Qt 6 kit root directory")
set(FFMPEG_DEV_ROOT "" CACHE PATH "FFmpeg development package root directory")
set(CINEVAULT_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version injected into the runtime")

if(QT_ROOT AND EXISTS "${QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
endif()

set(CINEVAULT_QT_COMPONENTS
    Core
    Gui
    Pdf
    PdfQuick
    Qml
    Quick
    QuickControls2
    Multimedia
    Network
    Widgets
)

if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
    list(APPEND CINEVAULT_QT_COMPONENTS
        Sql
        Concurrent
        WebSockets
    )
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS ${CINEVAULT_QT_COMPONENTS})

qt_standard_project_setup(REQUIRES 6.5)

set(CINEVAULT_HAS_BUNDLED_WEBP OFF)
set(CINEVAULT_LIBWEBP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libwebp")
if(EXISTS "${CINEVAULT_LIBWEBP_SOURCE_DIR}/CMakeLists.txt")
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CINEVAULT_LIBWEBP_SOURCE_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/third_party/libwebp"
                     EXCLUDE_FROM_ALL)
    set(CINEVAULT_HAS_BUNDLED_WEBP ON)
endif()

set(CINEVAULT_HAS_FFMPEG OFF)
if(CINEVAULT_ENABLE_FFMPEG AND NOT CINEVAULT_BUILD_MINIMAL_GUI)
    find_package(FFmpeg REQUIRED)
    set(CINEVAULT_HAS_FFMPEG ON)
endif()

set(APP_SOURCES
    src/app/main.cpp
    src/app/AppBootstrap.h
    src/app/AppBootstrap.cpp
    src/app/AppContext.h
    src/app/AppContext.cpp
    src/domain/Enums.h
    src/domain/Entities.h
    src/shared/Paths.h
    src/shared/Paths.cpp
    src/shared/Formatters.h
    src/shared/Formatters.cpp
    src/core/backup/BackupPlanner.h
    src/core/backup/BackupPlanner.cpp
    src/ui/imaging/LocalImageProvider.h
    src/ui/imaging/LocalImageProvider.cpp
    src/ui/imaging/LocalImageUrlHelper.h
    src/ui/imaging/LocalImageUrlHelper.cpp
    src/ui/window/WindowThemeController.h
    src/ui/window/WindowThemeController.cpp
    src/ui/models/SourceRootListModel.h
    src/ui/models/SourceRootListModel.cpp
    src/ui/models/ProjectLibraryListModel.h
    src/ui/models/ProjectLibraryListModel.cpp
    src/ui/models/AssetListModel.h
    src/ui/models/AssetListModel.cpp
    src/ui/models/BackupSourceListModel.h
    src/ui/models/BackupSourceListModel.cpp
    src/ui/models/BackupDestinationListModel.h
    src/ui/models/BackupDestinationListModel.cpp
    src/ui/models/BackupTaskListModel.h
    src/ui/models/BackupTaskListModel.cpp
    src/ui/models/JobListModel.h
    src/ui/models/JobListModel.cpp
)

set(APP_WINDOWS_RESOURCES)
if(WIN32)
    list(APPEND APP_WINDOWS_RESOURCES
        src/app/windows_app_icon.rc
    )
endif()

if(CINEVAULT_BUILD_MINIMAL_GUI)
    list(APPEND APP_SOURCES
        src/ui/viewmodels/MinimalShellViewModel.h
        src/ui/viewmodels/MinimalShellViewModel.cpp
        src/ui/viewmodels/MinimalSourceRailViewModel.h
        src/ui/viewmodels/MinimalSourceRailViewModel.cpp
        src/ui/viewmodels/MinimalImportWorkspaceViewModel.h
        src/ui/viewmodels/MinimalImportWorkspaceViewModel.cpp
        src/ui/viewmodels/MinimalMaterialBackupViewModel.h
        src/ui/viewmodels/MinimalMaterialBackupViewModel.cpp
        src/ui/viewmodels/MinimalLibraryWorkspaceViewModel.h
        src/ui/viewmodels/MinimalLibraryWorkspaceViewModel.cpp
        src/ui/viewmodels/MinimalInspectorViewModel.h
        src/ui/viewmodels/MinimalInspectorViewModel.cpp
        src/ui/viewmodels/MinimalJobTimelineViewModel.h
        src/ui/viewmodels/MinimalJobTimelineViewModel.cpp
        src/ui/viewmodels/MinimalReportWorkspaceViewModel.h
        src/ui/viewmodels/MinimalReportWorkspaceViewModel.cpp
    )
else()
    list(APPEND APP_SOURCES
    src/application/DocumentPreviewService.h
    src/application/DocumentPreviewService.cpp
    src/infrastructure/config/AppSettings.h
    src/infrastructure/config/AppSettings.cpp
    src/infrastructure/logging/Logger.h
    src/infrastructure/logging/Logger.cpp
    src/infrastructure/db/DatabaseManager.h
    src/infrastructure/db/DatabaseManager.cpp
    src/infrastructure/db/GlobalDatabaseManager.h
    src/infrastructure/db/GlobalDatabaseManager.cpp
    src/core/search/SearchEngine.h
    src/core/search/SearchEngine.cpp
    src/core/backup/BackupCopyEngine.h
    src/core/backup/BackupCopyEngine.cpp
    src/core/backup/VolumeEjectService.h
    src/core/backup/VolumeEjectService.cpp
    src/core/scan/FileTypeService.h
    src/core/scan/FileTypeService.cpp
    src/core/media/MediaProbeEngine.h
    src/core/media/MediaProbeEngine.cpp
    src/core/jobs/JobEngine.h
    src/core/jobs/JobEngine.cpp
    src/core/scan/ScanEngine.h
    src/core/scan/ScanEngine.cpp
    src/core/thumbnail/ThumbnailEngine.h
    src/core/thumbnail/ThumbnailEngine.cpp
    src/core/thumbnail/ContactSheetBuilder.h
    src/core/thumbnail/ContactSheetBuilder.cpp
    src/core/report/ReportModels.h
    src/core/report/ReportRenderEngine.h
    src/core/report/ReportRenderEngine.cpp
    src/application/ProjectService.h
    src/application/ProjectService.cpp
    src/application/JobService.h
    src/application/JobService.cpp
    src/application/MediaTaskService.h
    src/application/MediaTaskService.cpp
    src/application/MaterialCatalogSyncService.h
    src/application/MaterialCatalogSyncService.cpp
    src/application/MaterialCenterQueryService.h
    src/application/MaterialCenterQueryService.cpp
    src/application/MaterialBackupService.h
    src/application/MaterialBackupService.cpp
    src/application/VideoAnalysisService.h
    src/application/VideoAnalysisService.cpp
    src/application/ImportService.h
    src/application/ImportService.cpp
    src/application/LibraryQueryService.h
    src/application/LibraryQueryService.cpp
    src/application/FeedbackService.h
    src/application/FeedbackService.cpp
    src/application/ReportExportService.h
    src/application/ReportExportService.cpp
    src/application/UpdateService.h
    src/application/UpdateService.cpp
    src/infrastructure/ffmpeg/FFmpegAdapter.h
    src/infrastructure/ffmpeg/FFmpegAdapter.cpp
    src/infrastructure/network/VisionApiClient.h
    src/infrastructure/network/VisionApiClient.cpp
    src/infrastructure/network/VisionResponseParser.h
    src/infrastructure/network/VisionResponseParser.cpp
    src/ui/viewmodels/ProjectLibraryViewModel.h
    src/ui/viewmodels/ProjectLibraryViewModel.cpp
    src/ui/viewmodels/ShellViewModel.h
    src/ui/viewmodels/ShellViewModel.cpp
    src/ui/viewmodels/SourceRailViewModel.h
    src/ui/viewmodels/SourceRailViewModel.cpp
    src/ui/viewmodels/ImportWorkspaceViewModel.h
    src/ui/viewmodels/ImportWorkspaceViewModel.cpp
    src/ui/viewmodels/MaterialBackupViewModel.h
    src/ui/viewmodels/MaterialBackupViewModel.cpp
    src/ui/viewmodels/LibraryWorkspaceViewModel.h
    src/ui/viewmodels/LibraryWorkspaceViewModel.cpp
    src/ui/viewmodels/MaterialCenterViewModel.h
    src/ui/viewmodels/MaterialCenterViewModel.cpp
    src/ui/viewmodels/InspectorViewModel.h
    src/ui/viewmodels/InspectorViewModel.cpp
    src/ui/viewmodels/JobTimelineViewModel.h
    src/ui/viewmodels/JobTimelineViewModel.cpp
    src/ui/viewmodels/ReportWorkspaceViewModel.h
    src/ui/viewmodels/ReportWorkspaceViewModel.cpp
    src/ui/viewmodels/SettingsViewModel.h
    src/ui/viewmodels/SettingsViewModel.cpp
    src/ui/viewmodels/FeedbackViewModel.h
    src/ui/viewmodels/FeedbackViewModel.cpp
    src/ui/models/MaterialCenterListModel.h
    src/ui/models/MaterialCenterListModel.cpp
    src/ui/models/FeedbackMessageListModel.h
    src/ui/models/FeedbackMessageListModel.cpp
    )
endif()

qt_add_executable(CineVault
    ${APP_SOURCES}
    ${APP_WINDOWS_RESOURCES}
)

if(WIN32)
    set_target_properties(CineVault PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(CineVault PRIVATE Dwmapi Gdi32 Ole32 Shell32)
endif()

qt_add_resources(CineVault "CineVault_assets"
    PREFIX "/"
    BASE resources
    FILES
        resources/icons/app.ico
)

set_source_files_properties(src/ui/qml/themes/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(CineVault
    URI CineVault
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/themes/Theme.qml
        src/ui/qml/components/TopCommandBar.qml
        src/ui/qml/components/SourceRail.qml
        src/ui/qml/components/InspectorPane.qml
        src/ui/qml/components/JobProgressInspectorPane.qml
        src/ui/qml/components/JobTimelineBar.qml
        src/ui/qml/components/ActionButton.qml
        src/ui/qml/components/AssetPreviewOverlay.qml
        src/ui/qml/components/ThemedTextField.qml
        src/ui/qml/components/ThemedComboBox.qml
        src/ui/qml/components/ThemedSpinBox.qml
        src/ui/qml/components/ThemedProgressBar.qml
        src/ui/qml/components/ThemedSlider.qml
        src/ui/qml/components/ThemedScrollBar.qml
        src/ui/qml/components/ThemedMenu.qml
        src/ui/qml/components/ThemedMenuItem.qml
        src/ui/qml/components/ThemedMenuSeparator.qml
        src/ui/qml/components/SourceCard.qml
        src/ui/qml/components/StatusChip.qml
        src/ui/qml/components/AssetCard.qml
        src/ui/qml/components/VideoPreviewPlayer.qml
        src/ui/qml/components/SettingsDialog.qml
        src/ui/qml/workspaces/ImportWorkspace.qml
        src/ui/qml/workspaces/ProjectLibraryWorkspace.qml
        src/ui/qml/workspaces/MaterialBackupWorkspace.qml
        src/ui/qml/workspaces/LibraryWorkspace.qml
        src/ui/qml/workspaces/MaterialCenterWorkspace.qml
        src/ui/qml/workspaces/ReportWorkspace.qml
        src/ui/qml/workspaces/JobsWorkspace.qml
        src/ui/qml/workspaces/FeedbackWorkspace.qml
)

target_include_directories(CineVault PRIVATE src)

if(QT_ROOT)
    set(CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR "${QT_ROOT}/include/QtCore/${Qt6Core_VERSION}")
    if(EXISTS "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}/QtCore/private/qzipreader_p.h")
        target_include_directories(CineVault PRIVATE "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}")
    endif()
endif()

target_compile_definitions(CineVault PRIVATE
    CINEVAULT_BUILD_MINIMAL_GUI=$<IF:$<BOOL:${CINEVAULT_BUILD_MINIMAL_GUI}>,1,0>
    CINEVAULT_HAS_FFMPEG=$<IF:$<BOOL:${CINEVAULT_HAS_FFMPEG}>,1,0>
    CINEVAULT_HAS_BUNDLED_WEBP=$<IF:$<BOOL:${CINEVAULT_HAS_BUNDLED_WEBP}>,1,0>
    CINEVAULT_APP_VERSION="${CINEVAULT_APP_VERSION}"
)

target_link_libraries(CineVault PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Pdf
    Qt6::PdfQuick
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Network
    Qt6::Widgets
)

if(CINEVAULT_HAS_BUNDLED_WEBP)
    target_link_libraries(CineVault PRIVATE webpdecoder)
endif()

if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
    target_link_libraries(CineVault PRIVATE
        Qt6::Sql
        Qt6::Concurrent
        Qt6::WebSockets
    )
endif()

if(CINEVAULT_HAS_FFMPEG)
    target_link_libraries(CineVault PRIVATE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swscale
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(CineVaultSmokeTest tests/unit/SmokeTest.cpp)
    target_include_directories(CineVaultSmokeTest PRIVATE src)
    target_link_libraries(CineVaultSmokeTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME CineVaultSmokeTest COMMAND CineVaultSmokeTest)

    qt_add_executable(BackupPlannerTest
        tests/unit/BackupPlannerTest.cpp
        src/core/backup/BackupPlanner.cpp
    )
    target_include_directories(BackupPlannerTest PRIVATE src)
    target_link_libraries(BackupPlannerTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BackupPlannerTest COMMAND BackupPlannerTest)

    qt_add_executable(VisionApiClientJsonTest
        tests/unit/VisionApiClientJsonTest.cpp
        src/infrastructure/network/VisionResponseParser.cpp
    )
    target_include_directories(VisionApiClientJsonTest PRIVATE src)
    target_link_libraries(VisionApiClientJsonTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME VisionApiClientJsonTest COMMAND VisionApiClientJsonTest)

    qt_add_executable(UpdateServiceTest
        tests/unit/UpdateServiceTest.cpp
        src/application/UpdateService.cpp
        src/infrastructure/config/AppSettings.cpp
        src/shared/Paths.cpp
    )
    target_include_directories(UpdateServiceTest PRIVATE src)
    target_link_libraries(UpdateServiceTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME UpdateServiceTest COMMAND UpdateServiceTest)

    qt_add_executable(LocalImageUrlHelperTest
        tests/unit/LocalImageUrlHelperTest.cpp
        src/ui/imaging/LocalImageUrlHelper.cpp
    )
    target_include_directories(LocalImageUrlHelperTest PRIVATE src)
    target_link_libraries(LocalImageUrlHelperTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME LocalImageUrlHelperTest COMMAND LocalImageUrlHelperTest)

    qt_add_executable(JobListModelTest
        tests/unit/JobListModelTest.cpp
        src/ui/models/JobListModel.cpp
        src/shared/Formatters.cpp
    )
    target_include_directories(JobListModelTest PRIVATE src)
    target_link_libraries(JobListModelTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME JobListModelTest COMMAND JobListModelTest)

    qt_add_executable(JobEngineTest
        tests/unit/JobEngineTest.cpp
        src/core/jobs/JobEngine.cpp
        src/infrastructure/db/DatabaseManager.cpp
    )
    target_include_directories(JobEngineTest PRIVATE src)
    target_link_libraries(JobEngineTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME JobEngineTest COMMAND JobEngineTest)

    if(NOT CINEVAULT_BUILD_MINIMAL_GUI)
        qt_add_executable(DocumentPreviewServiceTest
            tests/unit/DocumentPreviewServiceTest.cpp
            src/application/DocumentPreviewService.cpp
        )
        target_include_directories(DocumentPreviewServiceTest PRIVATE src)
        if(QT_ROOT AND EXISTS "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}/QtCore/private/qzipreader_p.h")
            target_include_directories(DocumentPreviewServiceTest PRIVATE "${CINEVAULT_QTCORE_PRIVATE_INCLUDE_DIR}")
        endif()
        target_link_libraries(DocumentPreviewServiceTest PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME DocumentPreviewServiceTest COMMAND DocumentPreviewServiceTest)

        qt_add_executable(MaterialCenterQueryServiceTest
            tests/unit/MaterialCenterQueryServiceTest.cpp
            src/application/MaterialCenterQueryService.cpp
            src/core/search/SearchEngine.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/shared/Formatters.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(MaterialCenterQueryServiceTest PRIVATE src)
        target_link_libraries(MaterialCenterQueryServiceTest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
        add_test(NAME MaterialCenterQueryServiceTest COMMAND MaterialCenterQueryServiceTest)

        qt_add_executable(MaterialCatalogSyncServiceTest
            tests/unit/MaterialCatalogSyncServiceTest.cpp
            src/application/MaterialCatalogSyncService.cpp
            src/application/ProjectService.cpp
            src/core/jobs/JobEngine.cpp
            src/infrastructure/config/AppSettings.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/db/GlobalDatabaseManager.cpp
            src/infrastructure/logging/Logger.cpp
            src/shared/Formatters.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(MaterialCatalogSyncServiceTest PRIVATE src)
        target_compile_definitions(MaterialCatalogSyncServiceTest PRIVATE CINEVAULT_TESTING=1)
        target_link_libraries(MaterialCatalogSyncServiceTest PRIVATE Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Test)
        add_test(NAME MaterialCatalogSyncServiceTest COMMAND MaterialCatalogSyncServiceTest)

        qt_add_executable(ImportServiceLegacyRescanTest
            tests/unit/ImportServiceLegacyRescanTest.cpp
            src/application/ImportService.cpp
            src/application/JobService.cpp
            src/core/jobs/JobEngine.cpp
            src/core/scan/FileTypeService.cpp
            src/core/scan/ScanEngine.cpp
            src/infrastructure/db/DatabaseManager.cpp
        )
        target_include_directories(ImportServiceLegacyRescanTest PRIVATE src)
        target_link_libraries(ImportServiceLegacyRescanTest PRIVATE Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Test)
        add_test(NAME ImportServiceLegacyRescanTest COMMAND ImportServiceLegacyRescanTest)

        qt_add_executable(MediaTaskServiceRecoveryTest
            tests/unit/MediaTaskServiceRecoveryTest.cpp
            src/application/MediaTaskService.cpp
            src/core/jobs/JobEngine.cpp
            src/core/media/MediaProbeEngine.cpp
            src/core/thumbnail/ThumbnailEngine.cpp
            src/infrastructure/config/AppSettings.cpp
            src/infrastructure/db/DatabaseManager.cpp
            src/infrastructure/ffmpeg/FFmpegAdapter.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(MediaTaskServiceRecoveryTest PRIVATE src)
        target_link_libraries(MediaTaskServiceRecoveryTest PRIVATE Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Test)
        add_test(NAME MediaTaskServiceRecoveryTest COMMAND MediaTaskServiceRecoveryTest)

        qt_add_executable(FeedbackMessageListModelTest
            tests/unit/FeedbackMessageListModelTest.cpp
            src/ui/models/FeedbackMessageListModel.cpp
            src/shared/Formatters.cpp
        )
        target_include_directories(FeedbackMessageListModelTest PRIVATE src)
        target_link_libraries(FeedbackMessageListModelTest PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME FeedbackMessageListModelTest COMMAND FeedbackMessageListModelTest)

        qt_add_executable(FeedbackAttachmentCacheTest
            tests/unit/FeedbackAttachmentCacheTest.cpp
            src/application/FeedbackService.cpp
            src/infrastructure/config/AppSettings.cpp
            src/shared/Paths.cpp
        )
        target_include_directories(FeedbackAttachmentCacheTest PRIVATE src)
        target_link_libraries(FeedbackAttachmentCacheTest PRIVATE
            Qt6::Core
            Qt6::Network
            Qt6::Test
            Qt6::WebSockets
        )
        add_test(NAME FeedbackAttachmentCacheTest COMMAND FeedbackAttachmentCacheTest)
    endif()

    if(CINEVAULT_HAS_BUNDLED_WEBP)
        qt_add_executable(VisionApiClientImageTest
            tests/unit/VisionApiClientImageTest.cpp
            src/infrastructure/network/VisionApiClient.cpp
            src/infrastructure/network/VisionResponseParser.cpp
            src/infrastructure/logging/Logger.cpp
        )
        target_include_directories(VisionApiClientImageTest PRIVATE src)
        target_compile_definitions(VisionApiClientImageTest PRIVATE
            CINEVAULT_HAS_BUNDLED_WEBP=1
        )
        target_link_libraries(VisionApiClientImageTest PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Network
            Qt6::Test
            webpdecoder
        )
        add_test(NAME VisionApiClientImageTest COMMAND VisionApiClientImageTest)

        qt_add_executable(LocalImageProviderTest
            tests/unit/LocalImageProviderTest.cpp
            src/ui/imaging/LocalImageProvider.cpp
        )
        target_include_directories(LocalImageProviderTest PRIVATE src)
        target_compile_definitions(LocalImageProviderTest PRIVATE
            CINEVAULT_HAS_BUNDLED_WEBP=1
        )
        target_link_libraries(LocalImageProviderTest PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Test
            webpdecoder
        )
        add_test(NAME LocalImageProviderTest COMMAND LocalImageProviderTest)
    endif()

    set(CINEVAULT_TEST_QT_BIN_DIR "")
    if(TARGET Qt6::qmake)
        get_target_property(CINEVAULT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(CINEVAULT_TEST_QT_BIN_DIR "${CINEVAULT_QMAKE_EXECUTABLE}" DIRECTORY)
    elseif(QT_ROOT)
        set(CINEVAULT_TEST_QT_BIN_DIR "${QT_ROOT}/bin")
    endif()

    if(WIN32 AND CINEVAULT_TEST_QT_BIN_DIR)
        set(CINEVAULT_PATH_AWARE_TESTS
            CineVaultSmokeTest
            BackupPlannerTest
            VisionApiClientJsonTest
            UpdateServiceTest
            LocalImageUrlHelperTest
            JobListModelTest
            JobEngineTest
        )
        if(TARGET FeedbackMessageListModelTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS FeedbackMessageListModelTest)
        endif()
        if(TARGET DocumentPreviewServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS DocumentPreviewServiceTest)
        endif()
        if(TARGET MaterialCenterQueryServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MaterialCenterQueryServiceTest)
        endif()
        if(TARGET MaterialCatalogSyncServiceTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MaterialCatalogSyncServiceTest)
        endif()
        if(TARGET ImportServiceLegacyRescanTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS ImportServiceLegacyRescanTest)
        endif()
        if(TARGET MediaTaskServiceRecoveryTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS MediaTaskServiceRecoveryTest)
        endif()
        if(TARGET FeedbackAttachmentCacheTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS FeedbackAttachmentCacheTest)
        endif()
        if(TARGET LocalImageProviderTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS LocalImageProviderTest)
        endif()
        if(TARGET VisionApiClientImageTest)
            list(APPEND CINEVAULT_PATH_AWARE_TESTS VisionApiClientImageTest)
        endif()
        set_tests_properties(${CINEVAULT_PATH_AWARE_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CINEVAULT_TEST_QT_BIN_DIR}"
        )
    endif()
endif()

install(TARGETS CineVault
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
