#include "ui/models/MaterialCenterFolderListModel.h"
#include "ui/models/MaterialCenterFrameListModel.h"
#include "ui/models/MaterialCenterListModel.h"

#include <QFile>
#include <QtTest>

namespace {
QString sourceFile(const QString &relativePath)
{
    QFile file(QString::fromUtf8(CINEVAULT_SOURCE_DIR) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString sourceSection(const QString &source,
                      const QString &startMarker,
                      const QString &endMarker)
{
    const auto start = source.indexOf(startMarker);
    if (start < 0) {
        return {};
    }
    const auto end = source.indexOf(endMarker, start + startMarker.size());
    return end < 0 ? source.mid(start) : source.mid(start, end - start);
}

void verifyOrdered(const QString &source, const QStringList &contracts)
{
    qsizetype previous = -1;
    for (const auto &contract : contracts) {
        const auto current = source.indexOf(contract, previous + 1);
        QVERIFY2(current >= 0,
                 qPrintable(QStringLiteral("源码缺少或顺序错误：%1").arg(contract)));
        previous = current;
    }
}

GlobalVideoAsset sampleAsset()
{
    GlobalVideoAsset asset;
    asset.videoKey = QStringLiteral("asset-1");
    asset.assetKey = QStringLiteral("asset-key-1");
    asset.fileName = QStringLiteral("A001_C001.mov");
    asset.projectName = QStringLiteral("上海项目");
    asset.sourceRootName = QStringLiteral("Camera A");
    asset.relativePath = QStringLiteral("2026-07-14/A001_C001.mov");
    asset.assetType = AssetType::Video;
    asset.thumbnailPath = QStringLiteral("G:/cache/A001_C001.webp");
    asset.captureDate = QStringLiteral("2026-07-14");
    asset.captureTimeSource = QStringLiteral("quicktime_creation_date");
    asset.searchScore = 0.91;
    asset.searchConfidence = 0.88;
    asset.searchReasons = {QStringLiteral("拍摄日期命中"), QStringLiteral("视觉帧命中：00:01:24")};
    asset.matchedTimestampMs = 84000;
    asset.matchedFrameCaption = QStringLiteral("雨夜里人物撑着红伞");
    return asset;
}

FolderSearchHit sampleFolder()
{
    FolderSearchHit folder;
    folder.folderKey = QStringLiteral("folder-1");
    folder.projectUuid = QStringLiteral("project-1");
    folder.projectName = QStringLiteral("上海项目");
    folder.projectDatabasePath = QStringLiteral("G:/projects/shanghai/project.sqlite");
    folder.sourceRootId = 17;
    folder.sourceRootName = QStringLiteral("Camera A");
    folder.name = QStringLiteral("夜景航拍");
    folder.absolutePath = QStringLiteral("G:/media/2026-07-14/夜景航拍");
    folder.relativePath = QStringLiteral("2026-07-14/夜景航拍");
    folder.parentRelativePath = QStringLiteral("2026-07-14");
    folder.depth = 2;
    folder.directFileCount = 8;
    folder.recursiveFileCount = 13;
    folder.normalizedDate = QStringLiteral("2026-07-14");
    folder.available = true;
    folder.score = 0.82;
    folder.confidence = 0.76;
    folder.reasons = {QStringLiteral("目录日期命中"), QStringLiteral("文件夹名称或路径命中")};
    return folder;
}

FrameSearchHit sampleFrame()
{
    FrameSearchHit frame;
    frame.frameKey = QStringLiteral("frame:asset-1:61");
    frame.videoKey = QStringLiteral("asset-1");
    frame.assetKey = QStringLiteral("asset-key-1");
    frame.fileName = QStringLiteral("A001_C001.mov");
    frame.projectName = QStringLiteral("上海项目");
    frame.sourceRootName = QStringLiteral("Camera A");
    frame.relativePath = QStringLiteral("2026-07-14/A001_C001.mov");
    frame.assetType = AssetType::Video;
    frame.frameNumber = 61;
    frame.timestampMs = 2000;
    frame.imagePath = QStringLiteral("G:/cache/frame-61.jpg");
    frame.caption = QStringLiteral("模特穿着深蓝色牛仔裤坐在窗边");
    frame.tags = {QStringLiteral("蓝色"), QStringLiteral("牛仔")};
    frame.objects = {QStringLiteral("牛仔裤"), QStringLiteral("藤编椅")};
    frame.actions = QStringLiteral("坐在藤编椅上");
    frame.setting = QStringLiteral("室内窗边");
    VisionEntityFact jeans;
    jeans.label = QStringLiteral("长裤");
    jeans.colors = {QStringLiteral("蓝色")};
    jeans.materials = {QStringLiteral("牛仔")};
    frame.entities = {jeans};
    frame.factsComplete = true;
    frame.score = 0.93;
    frame.confidence = 0.89;
    frame.reasons = {QStringLiteral("帧视觉事实或文字命中"),
                     QStringLiteral("同一帧、同一视觉对象属性已验证")};
    return frame;
}
}

class MaterialCenterUiContractTest : public QObject {
    Q_OBJECT

private slots:
    void folderModelExposesNavigationAndRankingRoles()
    {
        MaterialCenterFolderListModel model;
        model.setItems({sampleFolder()});

        QCOMPARE(model.rowCount(), 1);
        const auto index = model.index(0, 0);
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::FolderKeyRole).toString(),
                 QStringLiteral("folder-1"));
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::ProjectDatabasePathRole).toString(),
                 QStringLiteral("G:/projects/shanghai/project.sqlite"));
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::AbsolutePathRole).toString(),
                 QStringLiteral("G:/media/2026-07-14/夜景航拍"));
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::RecursiveFileCountRole).toLongLong(),
                 qint64{13});
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::ScoreRole).toDouble(), 0.82);
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::ConfidenceRole).toDouble(), 0.76);
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::ReasonsRole).toString(),
                 QStringLiteral("目录日期命中 · 文件夹名称或路径命中"));
        QCOMPARE(model.data(index, MaterialCenterFolderListModel::ResultRankRole).toInt(), 1);

        const auto roles = model.roleNames();
        QCOMPARE(roles.value(MaterialCenterFolderListModel::FolderKeyRole), QByteArray("folderKey"));
        QCOMPARE(roles.value(MaterialCenterFolderListModel::ProjectDatabasePathRole),
                 QByteArray("projectDatabasePath"));
        QCOMPARE(roles.value(MaterialCenterFolderListModel::ScoreRole), QByteArray("score"));
        QCOMPARE(roles.value(MaterialCenterFolderListModel::ConfidenceRole), QByteArray("confidence"));
        QCOMPARE(roles.value(MaterialCenterFolderListModel::ReasonsRole), QByteArray("reasons"));
        QCOMPARE(roles.value(MaterialCenterFolderListModel::ResultRankRole), QByteArray("resultRank"));
    }

    void assetModelPreservesHybridResultOrder()
    {
        auto first = sampleAsset();
        auto second = sampleAsset();
        second.videoKey = QStringLiteral("asset-2");
        second.assetKey = QStringLiteral("asset-key-2");
        second.fileName = QStringLiteral("A001_C002.mov");

        MaterialCenterListModel model;
        model.setItems({first, second});

        QCOMPARE(model.rowCount({}), 2);
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::VideoKeyRole).toString(),
                 QStringLiteral("asset-1"));
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::CaptureDateRole).toString(),
                 QStringLiteral("2026-07-14"));
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::CaptureTimeSourceLabelRole).toString(),
                 QStringLiteral("QuickTime 拍摄时间"));
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::SearchConfidenceRole).toDouble(),
                 0.88);
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::MatchedTimestampLabelRole).toString(),
                 QStringLiteral("00:01:24"));
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::MatchedFrameCaptionRole).toString(),
                 QStringLiteral("雨夜里人物撑着红伞"));
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::ResultRankRole).toInt(), 1);
        QCOMPARE(model.data(model.index(0, 0), MaterialCenterListModel::QuickPreviewPathRole).toString(),
                 QStringLiteral("G:/cache/A001_C001.webp"));
        QVERIFY(model.data(model.index(0, 0), MaterialCenterListModel::QuickDetailRole).toString()
                    .contains(QStringLiteral("雨夜里人物撑着红伞")));
        QVERIFY(model.data(model.index(0, 0), MaterialCenterListModel::QuickMetaRole).toString()
                    .contains(QStringLiteral("视频")));
        QVERIFY(model.data(model.index(0, 0), MaterialCenterListModel::QuickReasonsRole).toString()
                    .contains(QStringLiteral("拍摄日期命中")));
        QCOMPARE(model.data(model.index(1, 0), MaterialCenterListModel::VideoKeyRole).toString(),
                 QStringLiteral("asset-2"));
        QCOMPARE(model.data(model.index(1, 0), MaterialCenterListModel::ResultRankRole).toInt(), 2);
    }

    void frameModelExposesThumbnailDetailsAndRankingRoles()
    {
        MaterialCenterFrameListModel model;
        model.setItems({sampleFrame()});

        QCOMPARE(model.rowCount(), 1);
        const auto index = model.index(0, 0);
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::FrameKeyRole).toString(),
                 QStringLiteral("frame:asset-1:61"));
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::FrameNumberRole).toInt(), 61);
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::TimestampLabelRole).toString(),
                 QStringLiteral("00:00:02"));
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::ImagePathRole).toString(),
                 QStringLiteral("G:/cache/frame-61.jpg"));
        QVERIFY(model.data(index, MaterialCenterFrameListModel::CaptionRole).toString()
                    .contains(QStringLiteral("深蓝色牛仔裤")));
        QVERIFY(model.data(index, MaterialCenterFrameListModel::EntitySummaryRole).toString()
                    .contains(QStringLiteral("长裤")));
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::ConfidenceRole).toDouble(), 0.89);
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::ResultRankRole).toInt(), 1);
        QCOMPARE(model.data(index, MaterialCenterFrameListModel::QuickPreviewPathRole).toString(),
                 QStringLiteral("G:/cache/frame-61.jpg"));
        QVERIFY(model.data(index, MaterialCenterFrameListModel::QuickDetailRole).toString()
                    .contains(QStringLiteral("深蓝色牛仔裤")));
        QVERIFY(model.data(index, MaterialCenterFrameListModel::QuickMetaRole).toString()
                    .contains(QStringLiteral("00:00:02")));
        QVERIFY(model.data(index, MaterialCenterFrameListModel::QuickReasonsRole).toString()
                    .contains(QStringLiteral("同一帧")));

        const auto roles = model.roleNames();
        QCOMPARE(roles.value(MaterialCenterFrameListModel::FrameKeyRole), QByteArray("frameKey"));
        QCOMPARE(roles.value(MaterialCenterFrameListModel::ImagePathRole), QByteArray("imagePath"));
        QCOMPARE(roles.value(MaterialCenterFrameListModel::EntitySummaryRole), QByteArray("entitySummary"));
        QCOMPARE(roles.value(MaterialCenterFrameListModel::ReasonsRole), QByteArray("reasons"));
    }

    void viewModelUsesHybridQueryAndKeepsEveryFilter()
    {
        const auto source = sourceFile(QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.cpp"));
        QVERIFY2(!source.isEmpty(), "无法读取 MaterialCenterViewModel.cpp");
        QVERIFY(source.contains(QStringLiteral("m_queryService->searchMaterials(m_searchText,")));
        QVERIFY(!source.contains(QStringLiteral("m_queryService->fetchAssets(")));
        QVERIFY(source.contains(QStringLiteral("scope.projectUuid = m_projectFilter")));
        QVERIFY(source.contains(QStringLiteral("scope.sourceRootName = m_sourceFilter")));
        QVERIFY(source.contains(QStringLiteral("scope.analysisStatusFilter = m_analysisStatusFilter")));
        QVERIFY(source.contains(QStringLiteral("scope.confirmationStatusFilter = -1")));
        QVERIFY(source.contains(QStringLiteral("scope.assetTypeFilter = m_assetTypeFilter")));
        QVERIFY(source.contains(QStringLiteral("scope.resultQuickFilter =")));
        QVERIFY(source.contains(QStringLiteral("m_folderModel->setItems(m_folders)")));
        QVERIFY(source.contains(QStringLiteral("m_frameModel->setItems(m_frames)")));
        QVERIFY(source.contains(QStringLiteral("m_semanticSearchAvailable = result.semanticSearchAvailable")));
        QVERIFY(source.contains(QStringLiteral("m_searchWarningMessage = result.warningMessage")));
        QVERIFY(source.contains(QStringLiteral("m_excludedPartialCount = result.excludedPartialCount")));
        QVERIFY(source.contains(QStringLiteral("m_searchInterpretationText = result.parsedQuery.interpretationLabels")));
        QVERIFY(source.contains(QStringLiteral("m_lastParsedQuery = result.parsedQuery")));
        QVERIFY(source.contains(QStringLiteral("m_searchEmptyReason")));
        QVERIFY(source.contains(QStringLiteral("SearchDocumentSyncService::synchronizationProgress")));
        QVERIFY(source.contains(QStringLiteral("SearchDocumentSyncService::synchronizationFinished")));
        QVERIFY(source.contains(QStringLiteral("m_searchRefreshTimer->start(0)")));
        QVERIFY(source.contains(QStringLiteral("understandQuery")));
        QVERIFY(!source.contains(QStringLiteral("rerankFrameCandidates")));
        QVERIFY(!source.contains(QStringLiteral("startFrameRerank")));
        QVERIFY(source.contains(QStringLiteral("task.generation != m_searchGeneration")));
    }

    void qmlConsumesSearchStateAndFolderActions()
    {
        const auto source = sourceFile(
            QStringLiteral("src/ui/qml/workspaces/MaterialCenterWorkspace.qml"));
        QVERIFY2(!source.isEmpty(), "无法读取 MaterialCenterWorkspace.qml");
        const QStringList contracts = {
            QStringLiteral("model: viewModel ? viewModel.folderModel : null"),
            QStringLiteral("model: viewModel ? viewModel.assetModel : null"),
            QStringLiteral("model: viewModel ? viewModel.frameModel : null"),
            QStringLiteral("viewModel.frameSearchMode"),
            QStringLiteral("id: frameResultGrid"),
            QStringLiteral("text: \"视觉帧命中\""),
            QStringLiteral("root.openImageViewer(root.imageFileSource(imagePath))"),
            QStringLiteral("visible: viewModel && !viewModel.frameSearchMode"),
            QStringLiteral("viewModel.semanticSearchAvailable"),
            QStringLiteral("viewModel.semanticSearchStatusText"),
            QStringLiteral("viewModel.semanticIndexing"),
            QStringLiteral("viewModel.semanticIndexProgress"),
            QStringLiteral("viewModel.semanticIndexStatusText"),
            QStringLiteral("viewModel.searchWarningMessage"),
            QStringLiteral("viewModel.searchInterpretationText"),
            QStringLiteral("viewModel.searchAssistantStatusText"),
            QStringLiteral("viewModel.searchEmptyReason"),
            QStringLiteral("viewModel.excludedPartialCount"),
            QStringLiteral("id: searchInput"),
            QStringLiteral("text: \"清空输入\""),
            QStringLiteral("enabled: searchInput.text.length > 0"),
            QStringLiteral("shellVm.globalSearchText = \"\""),
            QStringLiteral("searchInput.forceActiveFocus()"),
            QStringLiteral("text: \"搜索命中\""),
            QStringLiteral("viewModel.searchResultFilter === 0"),
            QStringLiteral("viewModel.setSearchResultFilter(1)"),
            QStringLiteral("viewModel.setSearchResultFilter(2)"),
            QStringLiteral("viewModel.setSearchResultFilter(3)"),
            QStringLiteral("viewModel.setSearchResultFilter(4)"),
            QStringLiteral("text: \"帧画面\""),
            QStringLiteral("text: \"图片\""),
            QStringLiteral("MiddleDragScrollHandler"),
            QStringLiteral("id: detailColumn"),
            QStringLiteral("contentHeight: detailColumn.implicitHeight"),
            QStringLiteral("ScrollBar.horizontal.policy: ScrollBar.AlwaysOff"),
            QStringLiteral("viewModel.openFolderProject(folderKey)"),
            QStringLiteral("viewModel.locateFolder(folderKey)"),
            QStringLiteral("viewModel.openAssetFolder(frameContextMenu.targetVideoKey)"),
            QStringLiteral("viewModel.copyAssetPath(frameContextMenu.targetVideoKey)"),
            QStringLiteral("viewModel.openAssetFolder(assetContextMenu.targetVideoKey)"),
            QStringLiteral("viewModel.copyAssetPath(assetContextMenu.targetVideoKey)"),
            QStringLiteral("viewModel.copyFolderPath(folderKey)"),
            QStringLiteral("text: \"打开所在目录\""),
            QStringLiteral("text: \"复制文件路径\""),
            QStringLiteral("viewModel.projectFilter"),
            QStringLiteral("viewModel.sourceFilter"),
            QStringLiteral("viewModel.analysisStatusFilter"),
            QStringLiteral("searchConfidence"),
            QStringLiteral("searchReasons"),
            QStringLiteral("confidence"),
            QStringLiteral("reasons"),
            QStringLiteral("function revealQuickSearchResult()"),
            QStringLiteral("viewModel.quickSearchRevealVideoKey"),
            QStringLiteral("viewModel.quickSearchRevealFrameNumber"),
            QStringLiteral("viewModel.quickSearchRevealIndex"),
            QStringLiteral("positionViewAtIndex(targetIndex"),
            QStringLiteral("id: quickSearchRevealTimer"),
            QStringLiteral("readonly property bool quickSearchReveal")
        };
        for (const auto &contract : contracts) {
            QVERIFY2(source.contains(contract), qPrintable(QStringLiteral("QML 缺少接口：%1").arg(contract)));
        }
        QVERIFY(source.contains(QStringLiteral("id: frameRankText")));
        QVERIFY(source.contains(QStringLiteral("id: assetRankText")));
        QVERIFY(!source.contains(QStringLiteral("text: \"全部确认\"")));
        QVERIFY(!source.contains(QStringLiteral("text: \"确认结果\"")));
        QVERIFY(!source.contains(QStringLiteral("viewModel.confirmationStatusFilter")));
        QVERIFY(!source.contains(QStringLiteral("viewModel.confirmVisible")));
        QVERIFY(!source.contains(QStringLiteral("viewModel.confirmSelected")));
    }

    void batchAnalysisRequiresExplicitSupplementOrRebuildPolicy()
    {
        const auto qml = sourceFile(QStringLiteral("src/ui/qml/workspaces/MaterialCenterWorkspace.qml"));
        const auto viewModelHeader = sourceFile(QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.h"));
        const auto viewModel = sourceFile(QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.cpp"));
        const auto serviceHeader = sourceFile(QStringLiteral("src/application/VideoAnalysisService.h"));
        const auto service = sourceFile(QStringLiteral("src/application/VideoAnalysisService.cpp"));
        QVERIFY2(!qml.isEmpty() && !viewModelHeader.isEmpty() && !viewModel.isEmpty()
                     && !serviceHeader.isEmpty() && !service.isEmpty(),
                 "无法读取批量解析策略契约源码");

        QVERIFY(qml.contains(QStringLiteral("text: \"多维度解析\"")));
        QVERIFY(qml.contains(QStringLiteral("选择多维度解析方式")));
        QVERIFY(qml.contains(QStringLiteral("补充解析（推荐）")));
        QVERIFY(qml.contains(QStringLiteral("完整帧直接跳过")));
        QVERIFY(qml.contains(QStringLiteral("设置中的自定义维度也会自动加入任务")));
        QVERIFY(qml.contains(QStringLiteral("viewModel.analyzeVisibleSupplement()")));
        QVERIFY(qml.contains(QStringLiteral("viewModel.analyzeVisibleAll()")));
        QVERIFY(qml.contains(QStringLiteral("Math.max(1, Math.min(620, root.width - 24))")));
        QVERIFY(qml.contains(QStringLiteral("Math.max(1, Math.min(430, root.height - 24))")));
        QVERIFY(qml.contains(QStringLiteral("id: multiDimensionAnalyzeScroll")));
        QVERIFY(qml.contains(QStringLiteral("implicitHeight: supplementOptionContent.implicitHeight + 28")));
        QVERIFY(!qml.contains(QStringLiteral("text: \"批量解析\"")));
        QVERIFY(!qml.contains(QStringLiteral("dimensionAnalyzeDialog")));
        QVERIFY(!qml.contains(QStringLiteral("dimensionDraftModel")));
        QVERIFY(!qml.contains(QStringLiteral("viewModel.analyzeVisiblePending()")));
        QVERIFY(!qml.contains(QStringLiteral("解析未完成素材")));

        QVERIFY(viewModelHeader.contains(QStringLiteral("Q_INVOKABLE void analyzeVisibleSupplement()")));
        QVERIFY(!viewModelHeader.contains(QStringLiteral("analyzeVisibleDimensions")));
        QVERIFY(!viewModelHeader.contains(QStringLiteral("canAnalyzeVisibleDimensions")));
        QVERIFY(viewModel.contains(QStringLiteral("asset.analysisStatus == VideoAnalysisStatus::Failed")));
        QVERIFY(viewModel.contains(QStringLiteral("asset.analysisStatus == VideoAnalysisStatus::Running")));
        QVERIFY(viewModel.contains(QStringLiteral("enqueueVideosForSupplement(videoKeys, &message)")));
        QVERIFY(viewModel.contains(QStringLiteral("enqueueVideosForRebuild(videoKeys, &message)")));
        QVERIFY(serviceHeader.contains(QStringLiteral("enqueueVideosForSupplement")));
        QVERIFY(serviceHeader.contains(QStringLiteral("enqueueVideosForRebuild")));

        const auto supplement = sourceSection(
            service,
            QStringLiteral("int VideoAnalysisService::enqueueVideosForSupplement"),
            QStringLiteral("int VideoAnalysisService::enqueueVideosForRebuild"));
        QVERIFY(supplement.contains(QStringLiteral("hasIncompleteVisualFrames")));
        QVERIFY(supplement.contains(QStringLiteral("enqueueConfiguredDimensionsIfNeeded")));
        QVERIFY(supplement.contains(QStringLiteral("AnalysisRunMode::Resume")));
        QVERIFY(supplement.contains(QStringLiteral("if (!hasVisualGap)")));
        QVERIFY(!supplement.contains(QStringLiteral("AnalysisRunMode::Rebuild")));
        QVERIFY(!supplement.contains(QStringLiteral("deleteAnalysisArtifacts")));

        const auto rebuild = sourceSection(
            service,
            QStringLiteral("int VideoAnalysisService::enqueueVideosForRebuild"),
            QStringLiteral("bool VideoAnalysisService::retryFrame"));
        QVERIFY(rebuild.contains(QStringLiteral("AnalysisRunMode::Rebuild")));
    }

    void customAnalysisDimensionsAreManagedInSettingsAndJoinedToRegularTasks()
    {
        const auto settingsHeader = sourceFile(QStringLiteral("src/infrastructure/config/AppSettings.h"));
        const auto settingsSource = sourceFile(QStringLiteral("src/infrastructure/config/AppSettings.cpp"));
        const auto viewModelHeader = sourceFile(QStringLiteral("src/ui/viewmodels/SettingsViewModel.h"));
        const auto viewModelSource = sourceFile(QStringLiteral("src/ui/viewmodels/SettingsViewModel.cpp"));
        const auto qml = sourceFile(QStringLiteral("src/ui/qml/components/SettingsPage.qml"));
        const auto serviceHeader = sourceFile(QStringLiteral("src/application/VideoAnalysisService.h"));
        const auto serviceSource = sourceFile(QStringLiteral("src/application/VideoAnalysisService.cpp"));
        QVERIFY2(!settingsHeader.isEmpty() && !settingsSource.isEmpty()
                     && !viewModelHeader.isEmpty() && !viewModelSource.isEmpty()
                     && !qml.isEmpty() && !serviceHeader.isEmpty() && !serviceSource.isEmpty(),
                 "无法读取自定义解析维度契约源码");

        QVERIFY(settingsHeader.contains(QStringLiteral("QStringList customAnalysisDimensions() const")));
        QVERIFY(settingsSource.contains(QStringLiteral("materialCenter/customAnalysisDimensions")));
        QVERIFY(settingsSource.contains(QStringLiteral("Qt::CaseInsensitive")));
        QVERIFY(viewModelHeader.contains(QStringLiteral("Q_PROPERTY(QStringList customAnalysisDimensions")));
        QVERIFY(viewModelHeader.contains(QStringLiteral("addCustomAnalysisDimension")));
        QVERIFY(viewModelHeader.contains(QStringLiteral("removeCustomAnalysisDimension")));
        QVERIFY(viewModelSource.contains(QStringLiteral("已包含在常规解析任务中")));

        QVERIFY(qml.contains(QStringLiteral("objectName: \"customAnalysisDimensionButton\"")));
        QVERIFY(qml.contains(QStringLiteral("objectName: \"customAnalysisDimensionDialog\"")));
        QVERIFY(qml.contains(QStringLiteral("objectName: \"customAnalysisDimensionGlassFlow\"")));
        QVERIFY(!qml.contains(QStringLiteral("implicitHeight: childrenRect.height")));
        QVERIFY(qml.contains(QStringLiteral("Qt.rgba(0.36, 0.61, 1.0, 0.15)")));
        QVERIFY(qml.contains(QStringLiteral("已并入常规解析")));
        QVERIFY(qml.contains(QStringLiteral("色彩风格")));
        QVERIFY(qml.contains(QStringLiteral("构图与镜头语言")));
        QVERIFY(qml.contains(QStringLiteral("品牌/文字线索")));
        QVERIFY(qml.contains(QStringLiteral("适用场景")));
        QVERIFY(qml.contains(QStringLiteral("情绪氛围")));

        QVERIFY(serviceHeader.contains(QStringLiteral("enqueueConfiguredDimensionsIfNeeded")));
        const auto finishSection = sourceSection(
            serviceSource,
            QStringLiteral("void VideoAnalysisService::finishCurrentAnalysis"),
            QStringLiteral("void VideoAnalysisService::reportAnalysisProgress"));
        QVERIFY(finishSection.contains(QStringLiteral("completedJob.mode != AnalysisRunMode::SingleFrame")));
        QVERIFY(finishSection.contains(QStringLiteral("enqueueConfiguredDimensionsIfNeeded(videoKey")));
    }

    void searchSettingsExposeLocalTextAssistantAndQuickSearchContracts()
    {
        const auto header = sourceFile(QStringLiteral("src/ui/viewmodels/SettingsViewModel.h"));
        const auto implementation = sourceFile(QStringLiteral("src/ui/viewmodels/SettingsViewModel.cpp"));
        const auto qml = sourceFile(QStringLiteral("src/ui/qml/components/SettingsPage.qml"));
        const auto appContext = sourceFile(QStringLiteral("src/app/AppContext.cpp"));
        QVERIFY2(!header.isEmpty() && !implementation.isEmpty() && !qml.isEmpty()
                     && !appContext.isEmpty(),
                 "无法读取搜索设置契约源码");

        const QStringList propertyContracts = {
            QStringLiteral("Q_PROPERTY(bool searchAssistantEnabled"),
            QStringLiteral("Q_PROPERTY(QString localSearchAssistantStatusText"),
            QStringLiteral("Q_PROPERTY(bool quickSearchEnabled"),
            QStringLiteral("Q_PROPERTY(QString quickSearchShortcut"),
            QStringLiteral("Q_PROPERTY(bool startAtLogin"),
            QStringLiteral("Q_PROPERTY(QString quickSearchStatusText"),
            QStringLiteral("void searchSettingsChanged()")
        };
        for (const auto &contract : propertyContracts) {
            QVERIFY2(header.contains(contract),
                     qPrintable(QStringLiteral("SettingsViewModel 缺少接口：%1").arg(contract)));
        }

        const QStringList qmlContracts = {
            QStringLiteral("智能搜索与隐私"),
            QStringLiteral("实时使用内置轻量文本模型辅助理解查询"),
            QStringLiteral("viewModel.localSearchAssistantStatusText"),
            QStringLiteral("像 Flow Launcher 一样"),
            QStringLiteral("root.draftQuickSearchShortcut"),
            QStringLiteral("viewModel.shortcutFromKeyEvent"),
            QStringLiteral("viewModel.quickSearchStatusText"),
            QStringLiteral("搜索始终在本机完成"),
            QStringLiteral("视觉接口仅用于素材导入和解析，不参与搜索")
        };
        for (const auto &contract : qmlContracts) {
            QVERIFY2(qml.contains(contract),
                     qPrintable(QStringLiteral("设置 QML 缺少契约：%1").arg(contract)));
        }

        verifyOrdered(qml, {
            QStringLiteral("root.draftVisionModel,"),
            QStringLiteral("root.draftSearchAssistantEnabled,"),
            QStringLiteral("root.draftQuickSearchEnabled,"),
            QStringLiteral("root.draftQuickSearchShortcut,"),
            QStringLiteral("root.draftStartAtLogin,"),
            QStringLiteral("root.draftCloseButtonBehavior,"),
            QStringLiteral("root.draftAnalysisMode,")
        });
        QVERIFY(!header.contains(QStringLiteral("dailySearchModelCallLimit")));
        QVERIFY(!header.contains(QStringLiteral("searchModelCallsToday")));
        QVERIFY(!qml.contains(QStringLiteral("每日搜索模型调用上限")));
        QVERIFY(!qml.contains(QStringLiteral("searchModelBudgetLabel")));
        QVERIFY(!header.contains(QStringLiteral("frameRerankEnabled")));
        QVERIFY(!header.contains(QStringLiteral("allowSearchFrameUpload")));
        QVERIFY(!qml.contains(QStringLiteral("候选帧缩略图")));
        QVERIFY(implementation.contains(QStringLiteral("emit searchSettingsChanged()")));
        QVERIFY(appContext.contains(QStringLiteral("&SettingsViewModel::searchSettingsChanged")));
    }

    void settingsSwitchLabelsFollowActiveTheme()
    {
        const auto settingsQml = sourceFile(QStringLiteral("src/ui/qml/components/SettingsPage.qml"));
        const auto switchQml = sourceFile(QStringLiteral("src/ui/qml/components/ThemedSwitch.qml"));
        QVERIFY2(!settingsQml.isEmpty() && !switchQml.isEmpty(),
                 "无法读取设置页主题开关源码");

        QCOMPARE(settingsQml.count(QStringLiteral("ThemedSwitch {")), 4);
        QVERIFY(switchQml.contains(QStringLiteral("color: control.enabled ? Theme.text : Theme.weak")));
        QVERIFY(switchQml.contains(QStringLiteral("palette.windowText: control.enabled ? Theme.text : Theme.weak")));
        QVERIFY(switchQml.contains(QStringLiteral("color: control.checked ? Theme.primaryBg : Theme.inputPressed")));
    }

    void settingsUsesFullScreenScrollablePage()
    {
        const auto mainQml = sourceFile(QStringLiteral("src/ui/qml/Main.qml"));
        const auto settingsQml = sourceFile(QStringLiteral("src/ui/qml/components/SettingsPage.qml"));
        QVERIFY2(!mainQml.isEmpty() && !settingsQml.isEmpty(),
                 "无法读取全屏设置页源码");

        QVERIFY(mainQml.contains(QStringLiteral("SettingsPage {")));
        QVERIFY(mainQml.contains(QStringLiteral("settingsPage.openPage()")));
        QVERIFY(mainQml.contains(QStringLiteral("visible: !settingsPage.opened")));
        QVERIFY(!mainQml.contains(QStringLiteral(
            "id: settingsPage\n        parent: Overlay.overlay")));
        QVERIFY(!mainQml.contains(QStringLiteral("MultiEffect {")));

        QVERIFY(settingsQml.contains(QStringLiteral("Item {\n    id: root")));
        QVERIFY(!settingsQml.contains(QStringLiteral("Dialog {\n    id: root")));
        QVERIFY(settingsQml.contains(QStringLiteral("readonly property bool opened: visible")));
        QVERIFY(settingsQml.contains(QStringLiteral("function openPage()")));
        QVERIFY(settingsQml.contains(QStringLiteral("function closePage()")));
        QVERIFY(settingsQml.contains(QStringLiteral("contentWidth: availableWidth")));
        QVERIFY(settingsQml.contains(QStringLiteral("contentHeight: settingsColumn.implicitHeight + 40")));
        QVERIFY(!settingsQml.contains(QStringLiteral("contentItem.boundsBehavior")));
        QVERIFY(settingsQml.contains(QStringLiteral("ScrollBar.vertical: ThemedScrollBar")));
        QVERIFY(settingsQml.contains(QStringLiteral("policy: ScrollBar.AsNeeded")));
        QVERIFY(settingsQml.contains(QStringLiteral("flickable: settingsScroll.contentItem")));
    }

    void packagedStartupProbeLoadsTheRealQmlRoot()
    {
        const auto mainSource = sourceFile(QStringLiteral("src/app/main.cpp"));
        const auto bootstrapSource = sourceFile(QStringLiteral("src/app/AppBootstrap.cpp"));
        const auto settingsSource = sourceFile(
            QStringLiteral("src/ui/viewmodels/SettingsViewModel.cpp"));
        QVERIFY2(!mainSource.isEmpty() && !bootstrapSource.isEmpty() && !settingsSource.isEmpty(),
                 "无法读取打包后启动探针源码");

        QVERIFY(mainSource.contains(QStringLiteral("--qml-startup-probe")));
        QVERIFY(bootstrapSource.contains(QStringLiteral("--qml-startup-probe")));
        QVERIFY(bootstrapSource.contains(QStringLiteral("[qml-startup-probe] root-loaded")));
        QVERIFY(bootstrapSource.contains(QStringLiteral("m_engine->loadFromModule(\"CineVault\", \"Main\")")));
        QVERIFY(bootstrapSource.contains(QStringLiteral("QML root object load failed.")));
        QVERIFY(bootstrapSource.contains(QStringLiteral("QCoreApplication::exit(0)")));

        const auto startupUpdateSection = sourceSection(
            settingsSource,
            QStringLiteral("void SettingsViewModel::beginStartupUpdateFlow()"),
            QStringLiteral("void SettingsViewModel::checkForUpdates()"));
        verifyOrdered(startupUpdateSection,
                      {QStringLiteral("--qml-startup-probe"),
                       QStringLiteral("return;"),
                       QStringLiteral("m_updateService->beginStartupFlow()")});
    }

    void sourceImportAcceptsTypedNetworkPaths()
    {
        const auto mainQml = sourceFile(QStringLiteral("src/ui/qml/Main.qml"));
        const auto shellHeader = sourceFile(QStringLiteral("src/ui/viewmodels/ShellViewModel.h"));
        const auto importSource = sourceFile(QStringLiteral("src/application/ImportService.cpp"));
        const auto volumeService = sourceFile(QStringLiteral("src/application/StorageVolumeService.cpp"));
        QVERIFY2(!mainQml.isEmpty() && !shellHeader.isEmpty() && !importSource.isEmpty()
                     && !volumeService.isEmpty(),
                  "无法读取素材源网络路径契约源码");

        QVERIFY(mainQml.contains(QStringLiteral("UNC 网络共享路径")));
        QVERIFY(mainQml.contains(QStringLiteral("root.shellViewModel.importSourcePath(sourcePathField.text)")));
        QVERIFY(shellHeader.contains(QStringLiteral("Q_INVOKABLE bool importSourcePath(const QString &directoryPath)")));
        QVERIFY(importSource.contains(QStringLiteral("FolderPathMetadata::normalizeSourcePath(directoryPath)")));
        QVERIFY(importSource.contains(QStringLiteral("目录不存在或网络路径不可访问")));
        QVERIFY(mainQml.contains(QStringLiteral("root.shellViewModel.storageVolumes")));
        QVERIFY(mainQml.contains(QStringLiteral("root.shellViewModel.importStorageVolume(modelData.rootPath)")));
        QVERIFY(mainQml.contains(QStringLiteral("点击后递归索引卷内全部可读文件")));
        QVERIFY(shellHeader.contains(QStringLiteral("Q_PROPERTY(QVariantList storageVolumes")));
        QVERIFY(shellHeader.contains(QStringLiteral("Q_INVOKABLE bool importStorageVolume")));
        QVERIFY(volumeService.contains(QStringLiteral("QStorageInfo::mountedVolumes()")));
    }

    void closeButtonOffersPromptTrayAndExitBehaviors()
    {
        const auto mainQml = sourceFile(QStringLiteral("src/ui/qml/Main.qml"));
        const auto settingsQml = sourceFile(QStringLiteral("src/ui/qml/components/SettingsPage.qml"));
        const auto settingsHeader = sourceFile(QStringLiteral("src/ui/viewmodels/SettingsViewModel.h"));
        const auto trayHeader = sourceFile(QStringLiteral("src/ui/window/QuickSearchController.h"));
        QVERIFY2(!mainQml.isEmpty() && !settingsQml.isEmpty()
                     && !settingsHeader.isEmpty() && !trayHeader.isEmpty(),
                 "无法读取关闭行为契约源码");

        QVERIFY(settingsHeader.contains(QStringLiteral("Q_PROPERTY(int closeButtonBehavior")));
        QVERIFY(trayHeader.contains(QStringLiteral("Q_PROPERTY(bool trayAvailable")));
        QVERIFY(settingsQml.contains(QStringLiteral("每次询问")));
        QVERIFY(settingsQml.contains(QStringLiteral("最小化到托盘")));
        QVERIFY(settingsQml.contains(QStringLiteral("直接退出软件")));
        QVERIFY(mainQml.contains(QStringLiteral("if (behavior === 2)")));
        QVERIFY(mainQml.contains(QStringLiteral("if (behavior === 1)")));
        QVERIFY(mainQml.contains(QStringLiteral("closeConfirmDialog.open()")));
        QVERIFY(mainQml.contains(QStringLiteral("Qt.quit()")));
    }

    void modelSearchUsesLocalTextAssistantOnly()
    {
        const auto source = sourceFile(QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.cpp"));
        const auto visionHeader = sourceFile(
            QStringLiteral("src/infrastructure/network/VisionApiClient.h"));
        QVERIFY2(!source.isEmpty() && !visionHeader.isEmpty(),
                 "无法读取搜索或视觉客户端源码");

        const auto understanding = sourceSection(
            source,
            QStringLiteral("void MaterialCenterViewModel::startSearchUnderstanding"),
            QStringLiteral("void MaterialCenterViewModel::setSearchText"));
        QVERIFY2(!understanding.isEmpty(), "无法定位查询理解门禁");
        verifyOrdered(understanding, {
            QStringLiteral("!m_settings->searchAssistantEnabled()"),
            QStringLiteral("SearchAssistantRouter::decide"),
            QStringLiteral("m_searchUnderstandingCache.find(cacheKey)"),
            QStringLiteral("m_localSearchAssistantRuntime->start()"),
            QStringLiteral("client->understandQuery")
        });
        QVERIFY(!understanding.contains(QStringLiteral("shouldUseAssistant")));
        QVERIFY(understanding.contains(QStringLiteral("已保留本地搜索")));
        QVERIFY(!understanding.contains(QStringLiteral("canUseSearchModel")));
        QVERIFY(!understanding.contains(QStringLiteral("tryConsumeSearchModelCall")));
        QVERIFY(!understanding.contains(QStringLiteral("预算")));

        QVERIFY(!source.contains(QStringLiteral("VisionApiClient")));
        QVERIFY(!source.contains(QStringLiteral("startFrameRerank")));
        QVERIFY(!source.contains(QStringLiteral("rerankFrameCandidates")));
        QVERIFY(!source.contains(QStringLiteral("候选帧缩略图")));
        QVERIFY(!visionHeader.contains(QStringLiteral("understandSearchQuery")));
        QVERIFY(!visionHeader.contains(QStringLiteral("rerankFrameCandidates")));
        QVERIFY(visionHeader.contains(QStringLiteral("analyzeFrame")));
    }

    void flowStyleQuickSearchHasGlobalHotkeyAndKeyboardContracts()
    {
        const auto controllerHeader = sourceFile(QStringLiteral("src/ui/window/QuickSearchController.h"));
        const auto controllerSource = sourceFile(QStringLiteral("src/ui/window/QuickSearchController.cpp"));
        const auto quickSearchQml = sourceFile(QStringLiteral("src/ui/qml/components/QuickSearchWindow.qml"));
        const auto materialCenterHeader = sourceFile(QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.h"));
        const auto materialCenterSource = sourceFile(QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.cpp"));
        const auto mainQml = sourceFile(QStringLiteral("src/ui/qml/Main.qml"));
        const auto shellHeader = sourceFile(QStringLiteral("src/ui/viewmodels/ShellViewModel.h"));
        const auto shellSource = sourceFile(QStringLiteral("src/ui/viewmodels/ShellViewModel.cpp"));
        const auto appContext = sourceFile(QStringLiteral("src/app/AppContext.cpp"));
        QVERIFY2(!controllerHeader.isEmpty() && !controllerSource.isEmpty()
                     && !quickSearchQml.isEmpty() && !materialCenterHeader.isEmpty()
                     && !materialCenterSource.isEmpty() && !mainQml.isEmpty()
                     && !shellHeader.isEmpty() && !shellSource.isEmpty()
                     && !appContext.isEmpty(),
                  "无法读取快捷搜索契约源码");

        const QStringList nativeContracts = {
            QStringLiteral("QAbstractNativeEventFilter"),
            QStringLiteral("RegisterHotKey"),
            QStringLiteral("UnregisterHotKey"),
            QStringLiteral("Alt+Space"),
            QStringLiteral("quickSearchRequested"),
            QStringLiteral("QSystemTrayIcon"),
            QStringLiteral("--background")
        };
        for (const auto &contract : nativeContracts) {
            QVERIFY2(controllerHeader.contains(contract) || controllerSource.contains(contract),
                     qPrintable(QStringLiteral("快捷搜索控制器缺少契约：%1").arg(contract)));
        }

        const QStringList qmlContracts = {
            QStringLiteral("Qt.FramelessWindowHint"),
            QStringLiteral("Qt.WindowStaysOnTopHint"),
            QStringLiteral("materialCenterViewModel.setSearchText"),
            QStringLiteral("materialCenterViewModel.folderModel"),
            QStringLiteral("materialCenterViewModel.frameModel"),
            QStringLiteral("materialCenterViewModel.assetModel"),
            QStringLiteral("Keys.onDownPressed"),
            QStringLiteral("Keys.onUpPressed"),
            QStringLiteral("Ctrl+Enter 定位"),
            QStringLiteral("sequence: \"Escape\""),
            QStringLiteral("property bool pinned: false"),
            QStringLiteral("objectName: \"quickSearchPinButton\""),
            QStringLiteral("text: root.pinned ? \"已固定\" : \"固定显示\""),
            QStringLiteral("materialCenterViewModel.prepareGlobalQuickSearch()"),
            QStringLiteral("function activatePrimaryResult(videoKeyValue, resultIndex, locateOnly)"),
            QStringLiteral("viewModel.openQuickSearchResultAtIndex(videoKeyValue, resultIndex)"),
            QStringLiteral("root.activatePrimaryResult(videoKeyValue, index, locateOnly)"),
            QStringLiteral("onDoubleClicked: resultDelegate.activate"),
            QStringLiteral("function activateFolderResult(folderKeyValue, locateOnly)"),
            QStringLiteral("viewModel.openQuickSearchFolderResult(folderKeyValue)"),
            QStringLiteral("root.activateFolderResult(folderKeyValue, locateOnly)"),
            QStringLiteral("required property string quickPreviewPath"),
            QStringLiteral("required property string quickDetail"),
            QStringLiteral("required property string quickMeta"),
            QStringLiteral("required property string quickReasons"),
            QStringLiteral("DragHandler"),
            QStringLiteral("root.startSystemMove()"),
            QStringLiteral("restoredWindowPosition"),
            QStringLiteral("rememberWindowPosition"),
            QStringLiteral("内置文本模型增强中"),
            QStringLiteral("打开详情  →"),
            QStringLiteral("text: \"搜索命中\""),
            QStringLiteral("materialCenterViewModel.searchResultFilter"),
            QStringLiteral("materialCenterViewModel.setSearchResultFilter(modelData.value)"),
            QStringLiteral("{ label: \"帧画面\", value: 2 }"),
            QStringLiteral("{ label: \"图片\", value: 3 }"),
            QStringLiteral("{ label: \"文档\", value: 4 }"),
            QStringLiteral("MiddleDragScrollHandler"),
            QStringLiteral("materialCenterViewModel.openAssetFolder(resultDelegate.videoKeyValue)"),
            QStringLiteral("materialCenterViewModel.copyAssetPath(resultDelegate.videoKeyValue)"),
            QStringLiteral("materialCenterViewModel.copyFolderPath(folderDelegate.folderKeyValue)"),
            QStringLiteral("双击 / Enter 打开")
        };
        for (const auto &contract : qmlContracts) {
            QVERIFY2(quickSearchQml.contains(contract),
                     qPrintable(QStringLiteral("快捷搜索 QML 缺少契约：%1").arg(contract)));
        }
        QVERIFY(!quickSearchQml.contains(QStringLiteral("视觉语言模型增强中")));
        QVERIFY(quickSearchQml.contains(QStringLiteral("!root.pinned")));
        QVERIFY(quickSearchQml.contains(QStringLiteral("forceCloseQuickSearch || !pinned")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("enterProjectFromLibrary()")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("shellViewModel.currentWorkspace")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("function enterMaterialCenter")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("dragOrigin")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("onTranslationChanged")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral(
            "if (root.materialCenterViewModel.openQuickSearchResultAtIndex(videoKeyValue, index))")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral(
            "if (root.materialCenterViewModel.openQuickSearchFolderResult(folderKeyValue))")));
        QVERIFY(controllerHeader.contains(
            QStringLiteral("Q_INVOKABLE bool restoreMainWindow(QObject *windowObject)")));
        QVERIFY(controllerSource.contains(QStringLiteral("restoreNativeWindowToForeground")));
        QVERIFY(controllerSource.contains(QStringLiteral("AttachThreadInput")));
        QVERIFY(controllerSource.contains(QStringLiteral("BringWindowToTop")));
        QVERIFY(controllerSource.contains(QStringLiteral("SetForegroundWindow")));
        QVERIFY(controllerSource.contains(QStringLiteral("GetForegroundWindow() == windowHandle")));
        QVERIFY(controllerSource.contains(QStringLiteral("kMainWindowRestoreRetryDelaysMs")));
        QVERIFY(controllerHeader.contains(QStringLiteral("isMainWindowForeground")));
        QVERIFY(controllerHeader.contains(QStringLiteral("mainWindowRestoreFinished")));
        QVERIFY(controllerSource.contains(QStringLiteral("HWND_TOPMOST")));
        QVERIFY(quickSearchQml.contains(
            QStringLiteral("controller.restoreMainWindow(mainWindow)")));
        QVERIFY(mainQml.contains(
            QStringLiteral("quickSearchController.restoreMainWindow(root)")));
        QVERIFY(quickSearchQml.contains(QStringLiteral("mainWindow.restoreToForeground()")));
        const auto quickSearchWindowRestore = sourceSection(
            quickSearchQml,
            QStringLiteral("function showMainWindow(forceCloseQuickSearch)"),
            QStringLiteral("function activateCurrent(locateOnly)"));
        verifyOrdered(quickSearchWindowRestore, {
            QStringLiteral("root.activateMainWindow()"),
            QStringLiteral("hideSearch()"),
            QStringLiteral("Qt.callLater(function()"),
            QStringLiteral("root.activateMainWindow()")
        });
        const auto rootResultActivation = sourceSection(
            quickSearchQml,
            QStringLiteral("function activateFolderResult(folderKeyValue, locateOnly)"),
            QStringLiteral("function activateCurrent(locateOnly)"));
        verifyOrdered(rootResultActivation, {
            QStringLiteral("viewModel.openQuickSearchFolderResult(folderKeyValue)"),
            QStringLiteral("showMainWindow(true)"),
            QStringLiteral("viewModel.openQuickSearchResultAtIndex(videoKeyValue, resultIndex)"),
            QStringLiteral("showMainWindow(true)")
        });
        const auto mainWindowRestore = sourceSection(
            mainQml,
            QStringLiteral("function restoreToForeground()"),
            QStringLiteral("function minimizeToTray()"));
        verifyOrdered(mainWindowRestore, {
            QStringLiteral("root.showNormal()"),
            QStringLiteral("root.raise()"),
            QStringLiteral("root.requestActivate()")
        });
        QVERIFY(materialCenterHeader.contains(QStringLiteral("prepareGlobalQuickSearch")));
        QVERIFY(materialCenterHeader.contains(QStringLiteral("openQuickSearchResult")));
        QVERIFY(materialCenterHeader.contains(QStringLiteral("openQuickSearchFolderResult")));
        QVERIFY(materialCenterHeader.contains(QStringLiteral("quickSearchRevealFrameNumber")));
        QVERIFY(materialCenterHeader.contains(QStringLiteral("quickSearchNavigationRequested")));
        QVERIFY(shellHeader.contains(QStringLiteral("enterMaterialCenterFromQuickSearch")));
        QVERIFY(appContext.contains(QStringLiteral("&MaterialCenterViewModel::quickSearchNavigationRequested")));
        QVERIFY(appContext.contains(QStringLiteral("&ShellViewModel::enterMaterialCenterFromQuickSearch")));
        const auto globalQuickSearch = sourceSection(
            materialCenterSource,
            QStringLiteral("void MaterialCenterViewModel::prepareGlobalQuickSearch()"),
            QStringLiteral("void MaterialCenterViewModel::executeSearch"));
        QVERIFY(globalQuickSearch.contains(QStringLiteral("m_projectFilter.clear()")));
        QVERIFY(globalQuickSearch.contains(QStringLiteral("m_sourceFilter.clear()")));
        QVERIFY(globalQuickSearch.contains(QStringLiteral("m_analysisStatusFilter = -1")));
        QVERIFY(!globalQuickSearch.contains(QStringLiteral("m_confirmationStatusFilter")));
        const auto openQuickResult = sourceSection(
            materialCenterSource,
            QStringLiteral("bool MaterialCenterViewModel::openQuickSearchResult"),
            QStringLiteral("void MaterialCenterViewModel::locateSelectedSource"));
        verifyOrdered(openQuickResult, {
            QStringLiteral("const auto targetAsset = assetForFileAction(normalizedKey)"),
            QStringLiteral("selectVideo(normalizedKey)"),
            QStringLiteral("openSelectedProject()"),
            QStringLiteral("m_quickSearchRevealVideoKey = normalizedKey"),
            QStringLiteral("m_projectFilter = targetProjectUuid"),
            QStringLiteral("reload()"),
            QStringLiteral("selectVideo(normalizedKey)"),
            QStringLiteral("emit quickSearchNavigationRequested(quickSearchQuery)")
        });
        const auto openQuickFolder = sourceSection(
            materialCenterSource,
            QStringLiteral("bool MaterialCenterViewModel::openQuickSearchFolderResult"),
            QStringLiteral("void MaterialCenterViewModel::locateSelectedSource"));
        verifyOrdered(openQuickFolder, {
            QStringLiteral("const auto quickSearchQuery = m_searchText"),
            QStringLiteral("m_projectService->openProject(folder.projectDatabasePath"),
            QStringLiteral("setSearchText(quickSearchQuery)"),
            QStringLiteral("m_projectFilter = folder.projectUuid.trimmed()"),
            QStringLiteral("reload()"),
            QStringLiteral("emit quickSearchNavigationRequested(quickSearchQuery)")
        });
        const auto shellQuickNavigation = sourceSection(
            shellSource,
            QStringLiteral("void ShellViewModel::enterMaterialCenterFromQuickSearch"),
            QStringLiteral("void ShellViewModel::setGlobalSearchText"));
        verifyOrdered(shellQuickNavigation, {
            QStringLiteral("m_projectEntered = true"),
            QStringLiteral("m_currentWorkspace = WorkspaceId::MaterialCenter"),
            QStringLiteral("emit stateChanged()")
        });
        QVERIFY(shellQuickNavigation.contains(QStringLiteral("Q_UNUSED(searchText)")));
        QVERIFY(!shellQuickNavigation.contains(QStringLiteral("m_globalSearchText = searchText")));
        QVERIFY(!shellQuickNavigation.contains(QStringLiteral("emit globalSearchTextChanged()")));
        QVERIFY(!shellQuickNavigation.contains(QStringLiteral("emit searchRequested")));
        QVERIFY(materialCenterSource.contains(QStringLiteral("sameProjectDatabasePath")));
        QVERIFY(shellQuickNavigation.contains(QStringLiteral("m_projectService->hasOpenProject()")));
        const QStringList neutralDarkPaletteContracts = {
            QStringLiteral("readonly property color quickBg: \"#0E1014\""),
            QStringLiteral("readonly property color quickHeader: \"#171A20\""),
            QStringLiteral("readonly property color quickSelected: \"#2A3039\""),
            QStringLiteral("readonly property color quickAccent: \"#C6CDD7\"")
        };
        for (const auto &contract : neutralDarkPaletteContracts) {
            QVERIFY2(quickSearchQml.contains(contract),
                     qPrintable(QStringLiteral("快捷搜索缺少中性深灰主题契约：%1").arg(contract)));
        }
        QVERIFY(!quickSearchQml.contains(QStringLiteral("Theme.selectedBg")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("Theme.selectedLine")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("Theme.blue")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("Theme.orange")));
        QVERIFY(!quickSearchQml.contains(QStringLiteral("model.")));
        QVERIFY(controllerHeader.contains(QStringLiteral("clampWindowPosition")));
        QVERIFY(controllerSource.contains(QStringLiteral("availableGeometry")));
        QVERIFY(mainQml.contains(QStringLiteral("sequence: \"Ctrl+K\"")));
        QVERIFY(mainQml.contains(QStringLiteral("QuickSearchWindow")));
    }

    void backgroundAnalysisStartsAfterOneHourOfSystemIdle()
    {
        const auto coordinator = sourceFile(
            QStringLiteral("src/application/BackgroundMaintenanceCoordinator.cpp"));
        const auto idleMonitor = sourceFile(
            QStringLiteral("src/application/SystemIdleMonitor.h"));
        const auto analysisService = sourceFile(
            QStringLiteral("src/application/VideoAnalysisService.cpp"));
        QVERIFY2(!coordinator.isEmpty() && !idleMonitor.isEmpty()
                     && !analysisService.isEmpty(),
                 "无法读取系统空闲自动解析契约源码");

        QVERIFY(coordinator.contains(
            QStringLiteral("m_systemIdleMonitor->start(60 * 60 * 1000, 5000)")));
        QVERIFY(coordinator.contains(QStringLiteral("电脑已空闲 1 小时")));
        QVERIFY(coordinator.contains(QStringLiteral("dispatchNextAnalysis()")));
        QVERIFY(coordinator.contains(QStringLiteral("enqueueVideo(videoKey")));
        QVERIFY(idleMonitor.contains(
            QStringLiteral("idleThresholdMs = 60 * 60 * 1000")));
        const auto finishAnalysis = sourceSection(
            analysisService,
            QStringLiteral("void VideoAnalysisService::finishCurrentAnalysis"),
            QStringLiteral("void VideoAnalysisService::reportAnalysisProgress"));
        QVERIFY(finishAnalysis.contains(
            QStringLiteral("enqueueConfiguredDimensionsIfNeeded(videoKey")));
        QVERIFY(!coordinator.contains(QStringLiteral("电脑已空闲 30 分钟")));
    }

    void searchAssistantPreloadsAndUnloadsAfterConfigurableAppIdleTime()
    {
        const auto settingsHeader = sourceFile(
            QStringLiteral("src/ui/viewmodels/SettingsViewModel.h"));
        const auto settingsSource = sourceFile(
            QStringLiteral("src/ui/viewmodels/SettingsViewModel.cpp"));
        const auto settingsQml = sourceFile(
            QStringLiteral("src/ui/qml/components/SettingsPage.qml"));
        const auto appSettings = sourceFile(
            QStringLiteral("src/infrastructure/config/AppSettings.cpp"));
        const auto appContext = sourceFile(QStringLiteral("src/app/AppContext.cpp"));
        const auto appBootstrap = sourceFile(QStringLiteral("src/app/AppBootstrap.cpp"));
        const auto lifecycle = sourceFile(
            QStringLiteral("src/application/SearchAssistantLifecycleController.cpp"));
        const auto idleMonitor = sourceFile(
            QStringLiteral("src/application/ApplicationIdleMonitor.cpp"));
        QVERIFY2(!settingsHeader.isEmpty() && !settingsSource.isEmpty()
                     && !settingsQml.isEmpty() && !appSettings.isEmpty()
                     && !appContext.isEmpty() && !appBootstrap.isEmpty()
                     && !lifecycle.isEmpty() && !idleMonitor.isEmpty(),
                 "无法读取模型自动预热与卸载契约源码");

        QVERIFY(settingsHeader.contains(
            QStringLiteral("Q_PROPERTY(int searchAssistantAutoUnloadMinutes")));
        QVERIFY(appSettings.contains(
            QStringLiteral("materialCenter/searchAssistantAutoUnloadMinutes")));
        QVERIFY(appSettings.contains(QStringLiteral(", 30).toInt()")));
        QVERIFY(settingsQml.contains(
            QStringLiteral("searchAssistantAutoUnloadMinutesInput")));
        QVERIFY(settingsQml.contains(
            QStringLiteral("searchAssistantAutoUnloadPreset")));
        QVERIFY(settingsQml.contains(QStringLiteral("30 分钟（默认）")));
        QVERIFY(settingsQml.contains(QStringLiteral("可直接输入 5–1440 分钟")));
        QVERIFY(appBootstrap.contains(
            QStringLiteral("m_context->startInteractiveServices()")));
        QVERIFY(appBootstrap.contains(
            QStringLiteral("--search-assistant-startup-probe")));
        QVERIFY(appContext.contains(
            QStringLiteral("m_searchAssistantLifecycleController->start()")));
        QVERIFY(appContext.contains(QStringLiteral("quickSearchRequested")));
        QVERIFY(lifecycle.contains(QStringLiteral("m_runtime->stop()")));
        QVERIFY(lifecycle.contains(QStringLiteral("schedulePreload(0)")));
        QVERIFY(idleMonitor.contains(QStringLiteral("QEvent::KeyPress")));
        QVERIFY(idleMonitor.contains(QStringLiteral("QEvent::MouseMove")));
        QVERIFY(idleMonitor.contains(QStringLiteral("QEvent::TouchBegin")));
        QVERIFY(settingsSource.contains(QStringLiteral("打开搜索或输入查询时智能加载")));
    }

    void localSearchAssistantUsesLockedVulkanGpuRuntime()
    {
        const auto runtime = sourceFile(
            QStringLiteral("src/infrastructure/search/LocalSearchAssistantRuntime.cpp"));
        const auto settings = sourceFile(QStringLiteral("src/ui/viewmodels/SettingsViewModel.cpp"));
        const auto lock = sourceFile(QStringLiteral("cmake/search-assistant-dependencies.lock.json"));
        const auto prepare = sourceFile(
            QStringLiteral("cmake/PrepareSearchAssistantDependencies.cmake"));
        const auto cmake = sourceFile(QStringLiteral("CMakeLists.txt"));
        QVERIFY2(!runtime.isEmpty() && !settings.isEmpty() && !lock.isEmpty()
                     && !prepare.isEmpty() && !cmake.isEmpty(),
                 "无法读取内置文本模型 GPU 契约源码");

        QVERIFY(runtime.contains(QStringLiteral("--list-devices")));
        QVERIFY(runtime.contains(QStringLiteral("--n-gpu-layers")));
        QVERIFY(runtime.contains(QStringLiteral("QStringLiteral(\"99\")")));
        QVERIFY(runtime.contains(QStringLiteral("--sleep-idle-seconds")));
        QVERIFY(runtime.contains(QStringLiteral("QStringLiteral(\"-1\")")));
        QVERIFY(!runtime.contains(QStringLiteral("QStringLiteral(\"120\")")));
        QVERIFY(runtime.contains(QStringLiteral("不会回落到 CPU")));
        QVERIFY(settings.contains(QStringLiteral("本地模型已就绪（GPU：%1）；")));
        QVERIFY(!settings.contains(QStringLiteral("已就绪（CPU）")));
        QVERIFY(lock.contains(QStringLiteral("llama-cpp-windows-x64-vulkan")));
        QVERIFY(lock.contains(QStringLiteral("llama-b10012-bin-win-vulkan-x64.zip")));
        QVERIFY(!lock.contains(QStringLiteral("win-cpu-x64")));
        QVERIFY(prepare.contains(QStringLiteral("win-vulkan-x64")));
        QVERIFY(cmake.contains(QStringLiteral("win-vulkan-x64")));
    }

    void assetContextMenusExposeFolderAndClipboardActions()
    {
        const auto quickSearch = sourceFile(
            QStringLiteral("src/ui/qml/components/QuickSearchWindow.qml"));
        const auto materialCenter = sourceFile(
            QStringLiteral("src/ui/qml/workspaces/MaterialCenterWorkspace.qml"));
        const auto library = sourceFile(
            QStringLiteral("src/ui/qml/workspaces/LibraryWorkspace.qml"));
        const auto materialHeader = sourceFile(
            QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.h"));
        const auto materialSource = sourceFile(
            QStringLiteral("src/ui/viewmodels/MaterialCenterViewModel.cpp"));
        const auto libraryHeader = sourceFile(
            QStringLiteral("src/ui/viewmodels/LibraryWorkspaceViewModel.h"));
        const auto minimalLibraryHeader = sourceFile(
            QStringLiteral("src/ui/viewmodels/MinimalLibraryWorkspaceViewModel.h"));
        const auto fileRevealService = sourceFile(
            QStringLiteral("src/shared/FileRevealService.cpp"));
        QVERIFY2(!quickSearch.isEmpty() && !materialCenter.isEmpty() && !library.isEmpty()
                     && !materialHeader.isEmpty() && !materialSource.isEmpty()
                      && !libraryHeader.isEmpty() && !minimalLibraryHeader.isEmpty()
                      && !fileRevealService.isEmpty(),
                 "无法读取素材右键文件操作契约源码");

        for (const auto &qml : {quickSearch, materialCenter, library}) {
            QVERIFY(qml.contains(QStringLiteral("text: \"打开所在目录\"")));
            QVERIFY(qml.contains(QStringLiteral("text: \"复制文件路径\"")));
            QVERIFY(qml.contains(QStringLiteral("Qt.RightButton")));
        }
        QVERIFY(quickSearch.contains(QStringLiteral("copyFolderPath(folderDelegate.folderKeyValue)")));
        QVERIFY(quickSearch.contains(QStringLiteral("copyAssetPath(resultDelegate.videoKeyValue)")));
        QVERIFY(materialCenter.contains(QStringLiteral("id: frameContextMenu")));
        QVERIFY(materialCenter.contains(QStringLiteral("id: folderContextMenu")));
        QVERIFY(materialCenter.contains(QStringLiteral("id: assetContextMenu")));
        QVERIFY(materialCenter.count(QStringLiteral("property string targetVideoKey")) >= 2);
        QVERIFY(materialCenter.contains(
            QStringLiteral("frameContextMenu.targetVideoKey = videoKey")));
        QVERIFY(materialCenter.contains(
            QStringLiteral("assetContextMenu.targetVideoKey = videoKey")));
        QVERIFY(library.count(QStringLiteral("viewModel.copyAssetPath(assetId)")) >= 2);
        QVERIFY(materialHeader.contains(QStringLiteral("openAssetFolder(const QString &videoKey)")));
        QVERIFY(materialHeader.contains(QStringLiteral("copyAssetPath(const QString &videoKey)")));
        QVERIFY(materialHeader.contains(QStringLiteral("copyFolderPath(const QString &folderKey)")));
        QVERIFY(materialSource.contains(QStringLiteral("QGuiApplication::clipboard()")));
        QVERIFY(materialSource.contains(QStringLiteral("QDir::toNativeSeparators")));
        QVERIFY(materialSource.contains(QStringLiteral("FileRevealService::revealFile")));
        QVERIFY(fileRevealService.contains(QStringLiteral("SHOpenFolderAndSelectItems")));
        QVERIFY(fileRevealService.contains(QStringLiteral("/select,")));
        QVERIFY(fileRevealService.contains(QStringLiteral("explorer.exe")));
        QVERIFY(libraryHeader.contains(QStringLiteral("copyAssetPath(qint64 assetId)")));
        QVERIFY(minimalLibraryHeader.contains(QStringLiteral("copyAssetPath(qint64 assetId)")));
    }

    void middleButtonAnchorScrollIsReusableAndGloballyCovered()
    {
        const auto handler = sourceFile(
            QStringLiteral("src/ui/qml/components/MiddleDragScrollHandler.qml"));
        const auto cmake = sourceFile(QStringLiteral("CMakeLists.txt"));
        QVERIFY2(!handler.isEmpty() && !cmake.isEmpty(),
                 "无法读取中键拖动组件或 QML 清单");

        const QStringList contracts = {
            QStringLiteral("acceptedButtons: Qt.MiddleButton"),
            QStringLiteral("acceptedDevices: PointerDevice.Mouse"),
            QStringLiteral("target: null"),
            QStringLiteral("property Item anchorIndicator"),
            QStringLiteral("visible: handler.active"),
            QStringLiteral("property real deadZone: 10"),
            QStringLiteral("function stepForDistance(distance)"),
            QStringLiteral("property Timer autoScrollTimer"),
            QStringLiteral("horizontalFlickable"),
            QStringLiteral("verticalFlickable"),
            QStringLiteral("cancelFlick()"),
            QStringLiteral("activeTranslation.x"),
            QStringLiteral("activeTranslation.y"),
            QStringLiteral("returnToBounds()")
        };
        for (const auto &contract : contracts) {
            QVERIFY2(handler.contains(contract),
                     qPrintable(QStringLiteral("中键拖动组件缺少契约：%1").arg(contract)));
        }
        QVERIFY(cmake.contains(
            QStringLiteral("src/ui/qml/components/MiddleDragScrollHandler.qml")));

        const QStringList coveredQmlFiles = {
            QStringLiteral("src/ui/qml/components/QuickSearchWindow.qml"),
            QStringLiteral("src/ui/qml/components/SettingsPage.qml"),
            QStringLiteral("src/ui/qml/components/SourceRail.qml"),
            QStringLiteral("src/ui/qml/components/InspectorPane.qml"),
            QStringLiteral("src/ui/qml/components/JobProgressInspectorPane.qml"),
            QStringLiteral("src/ui/qml/components/AssetPreviewOverlay.qml"),
            QStringLiteral("src/ui/qml/components/ThemedComboBox.qml"),
            QStringLiteral("src/ui/qml/workspaces/ProjectLibraryWorkspace.qml"),
            QStringLiteral("src/ui/qml/workspaces/LibraryWorkspace.qml"),
            QStringLiteral("src/ui/qml/workspaces/MaterialCenterWorkspace.qml"),
            QStringLiteral("src/ui/qml/workspaces/ReportWorkspace.qml"),
            QStringLiteral("src/ui/qml/workspaces/JobsWorkspace.qml"),
            QStringLiteral("src/ui/qml/workspaces/FeedbackWorkspace.qml")
        };
        for (const auto &path : coveredQmlFiles) {
            const auto qml = sourceFile(path);
            QVERIFY2(qml.contains(QStringLiteral("MiddleDragScrollHandler")),
                     qPrintable(QStringLiteral("滚动页面未接入中键锚点：%1").arg(path)));
        }
        const auto library = sourceFile(
            QStringLiteral("src/ui/qml/workspaces/LibraryWorkspace.qml"));
        QVERIFY(library.contains(QStringLiteral("verticalFlickable: tableList")));
    }
};

QTEST_APPLESS_MAIN(MaterialCenterUiContractTest)

#include "MaterialCenterUiContractTest.moc"
