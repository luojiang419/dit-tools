#include "ui/viewmodels/MaterialCenterViewModel.h"

#include "shared/FileRevealService.h"

#include "application/MaterialCatalogSyncService.h"
#include "application/MaterialCenterQueryService.h"
#include "application/ProjectService.h"
#include "application/SearchDocumentSyncService.h"
#include "application/VideoAnalysisService.h"
#include "core/search/SearchQueryUnderstanding.h"
#include "core/search/SearchAssistantRouter.h"
#include "core/search/SearchResultFusion.h"
#include "core/search/SearchEngine.h"
#include "core/search/SemanticSearchIndexService.h"
#include "core/thumbnail/ContactSheetBuilder.h"
#include "infrastructure/config/AppSettings.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "infrastructure/search/LocalSearchAssistantRuntime.h"
#include "infrastructure/search/SearchAssistantClient.h"
#include "shared/Formatters.h"
#include "shared/Paths.h"
#include "shared/VisualAnalysisMetadata.h"
#include "ui/models/MaterialCenterFolderListModel.h"
#include "ui/models/MaterialCenterFrameListModel.h"
#include "ui/models/MaterialCenterListModel.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <memory>
#include <optional>

namespace {
constexpr int kInitialVisibleFrameCount = 24;
constexpr int kVisibleFrameBatchSize = 24;
constexpr int kMaxFrameRetryCount = 3;

bool sameProjectDatabasePath(const QString &left, const QString &right)
{
    const auto trimmedLeft = left.trimmed();
    const auto trimmedRight = right.trimmed();
    if (trimmedLeft.isEmpty() || trimmedRight.isEmpty()) {
        return false;
    }
    const auto normalizedLeft = QDir::cleanPath(QFileInfo(trimmedLeft).absoluteFilePath());
    const auto normalizedRight = QDir::cleanPath(QFileInfo(trimmedRight).absoluteFilePath());
    return !normalizedLeft.isEmpty()
        && !normalizedRight.isEmpty()
        && normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
}

QVariantList prependAllOption(const QVariantList &items, const QString &label)
{
    QVariantList options;
    options.append(QVariantMap{{QStringLiteral("value"), QString()}, {QStringLiteral("label"), label}});
    for (const auto &item : items) {
        options.append(item);
    }
    return options;
}

QVariantList stringListToVariants(const QStringList &items)
{
    QVariantList values;
    for (const auto &item : items) {
        values.append(item);
    }
    return values;
}

QVariantList dimensionAnalysesToVariants(const QVector<MaterialDimensionAnalysis> &items)
{
    QVariantList values;
    for (const auto &item : items) {
        values.append(QVariantMap{
            {QStringLiteral("name"), item.name},
            {QStringLiteral("detail"), item.detail},
            {QStringLiteral("analyzedAt"), item.analyzedAt}
        });
    }
    return values;
}

QStringList searchTerms(const QString &text)
{
    return text.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QVariantMap assetTypeOption(int value, const QString &label)
{
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
}

QVariantList fileTypeOptions()
{
    return {
        assetTypeOption(-1, QStringLiteral("全部文件")),
        assetTypeOption(static_cast<int>(AssetType::Video), QStringLiteral("视频文件")),
        assetTypeOption(static_cast<int>(AssetType::Audio), QStringLiteral("音频文件")),
        assetTypeOption(static_cast<int>(AssetType::Image), QStringLiteral("图片文件")),
        assetTypeOption(static_cast<int>(AssetType::Subtitle), QStringLiteral("字幕文件")),
        assetTypeOption(static_cast<int>(AssetType::ProjectFile), QStringLiteral("工程文件")),
        assetTypeOption(static_cast<int>(AssetType::Document), QStringLiteral("文档文件")),
        assetTypeOption(static_cast<int>(AssetType::Archive), QStringLiteral("压缩文件")),
        assetTypeOption(static_cast<int>(AssetType::Other), QStringLiteral("其他文件")),
        assetTypeOption(static_cast<int>(AssetType::Unknown), QStringLiteral("未识别文件"))
    };
}

QString frameSearchText(const FrameAnalysisRecord &frame)
{
    const auto entityTerms = VisualAnalysisMetadata::entityFactSearchTerms(frame.entities);
    return QStringList{
        frame.caption,
        frame.tags.join(QStringLiteral(" ")),
        frame.objects.join(QStringLiteral(" ")),
        frame.actions,
        frame.setting,
        entityTerms.join(QStringLiteral(" ")),
        frame.ocrText,
        frame.errorMessage
    }.join(QStringLiteral(" "));
}

QStringList matchedFrameTerms(const FrameAnalysisRecord &frame, const QStringList &terms)
{
    QStringList matches;
    const auto haystack = frameSearchText(frame);
    for (const auto &term : terms) {
        if (haystack.contains(term, Qt::CaseInsensitive)) {
            matches.append(term);
        }
    }
    matches.removeDuplicates();
    return matches;
}

bool frameMatches(const FrameAnalysisRecord &frame, const QStringList &terms)
{
    return terms.isEmpty() || !matchedFrameTerms(frame, terms).isEmpty();
}

QStringList contactSheetImagePaths(const VideoAnalysisDetail &detail, int contactSheetFrameCount)
{
    QSet<int> plannedNumbers;
    if (detail.hasVisualAnalysisPlan) {
        for (const auto frameNumber : VisualAnalysisMetadata::contactSheetFrameNumbers(
                 detail.visualAnalysisPlan.sourceFrameCount,
                 contactSheetFrameCount)) {
            plannedNumbers.insert(frameNumber);
        }
    }

    QStringList imagePaths;
    imagePaths.reserve(detail.frames.size());
    for (const auto &frame : detail.frames) {
        if (!plannedNumbers.isEmpty() && !plannedNumbers.contains(frame.frameNumber)) {
            continue;
        }
        if (!frame.imagePath.trimmed().isEmpty()) {
            imagePaths.append(frame.imagePath);
        }
    }
    return imagePaths;
}

int persistedAnalysisProgress(const GlobalVideoAsset &asset)
{
    if (asset.analysisStatus == VideoAnalysisStatus::Ready) {
        return 100;
    }

    const auto &task = asset.analysisTask;
    if (task.stage == VideoAnalysisTaskStage::Summarizing && task.totalFrames > 0) {
        return 90;
    }
    if (task.totalFrames <= 0) {
        return 0;
    }

    const auto completedProgress = 10 + ((task.completedFrames * 75) / qMax(1, task.totalFrames));
    return qBound(0, completedProgress, 90);
}

QString failedAnalysisHint(const GlobalVideoAsset &asset)
{
    const auto &task = asset.analysisTask;
    if (task.stage == VideoAnalysisTaskStage::Summarizing && task.totalFrames > 0) {
        return QStringLiteral("帧图已完成，汇总失败，可继续解析。");
    }
    if (task.totalFrames > 0) {
        return QStringLiteral("解析中断，已完成 %1/%2 帧，可继续解析。")
            .arg(task.completedFrames)
            .arg(task.totalFrames);
    }
    return QStringLiteral("解析失败，可继续解析。");
}

QString readyAnalysisHint(const GlobalVideoAsset &asset)
{
    if (asset.analysisTask.skippedFrames > 0) {
        return QStringLiteral("解析完成，已跳过 %1 帧，可手动补解析。").arg(asset.analysisTask.skippedFrames);
    }
    return QStringLiteral("解析完成，结果已自动生效，可按需重新解析。");
}

bool canRetryFrame(const FrameAnalysisRecord &frame)
{
    return frame.analysisState == FrameAnalysisState::Failed || frame.analysisState == FrameAnalysisState::Skipped;
}

QString retryLabel(const FrameAnalysisRecord &frame)
{
    if (frame.retryCount <= 0) {
        return {};
    }
    return QStringLiteral("已重试 %1/%2").arg(frame.retryCount).arg(kMaxFrameRetryCount);
}

bool isSupportedTextAsset(AssetType assetType, const QString &extension)
{
    static const QSet<QString> textExtensions = {
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("md"),
        QStringLiteral("json"), QStringLiteral("csv"), QStringLiteral("tsv"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("docx"), QStringLiteral("xlsx"), QStringLiteral("pptx"),
        QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("vtt")
    };
    const auto normalizedExtension = extension.trimmed().toLower();
    return assetType == AssetType::Subtitle
        || (assetType == AssetType::Document && textExtensions.contains(normalizedExtension));
}

bool canAnalyzeAsset(const GlobalVideoAsset &asset)
{
    return asset.assetType == AssetType::Video
        || asset.assetType == AssetType::Image
        || isSupportedTextAsset(asset.assetType, asset.extension);
}

QString previewText(QString text)
{
    text = text.simplified();
    if (text.size() <= 500) {
        return text;
    }
    return text.left(500).trimmed() + QStringLiteral("...");
}

struct SearchUnderstandingTaskResult {
    QString cacheKey;
    QString queryText;
    std::optional<ModelSearchUnderstanding> understanding;
    QString errorMessage;
};

struct MaterialCenterSearchTaskResult {
    int logicalGeneration = 0;
    int requestGeneration = 0;
    bool enhanced = false;
    QVariantList projectOptions;
    QVariantList sourceOptions;
    MaterialSearchResult result;
    QString errorMessage;
};

struct MaterialCenterDetailTaskResult {
    int requestGeneration = 0;
    QString videoKey;
    bool replaceCurrent = true;
    VideoAnalysisDetailPage page;
    QString errorMessage;
};

struct ContactSheetTaskResult {
    int requestGeneration = 0;
    QString videoKey;
    QString outputPath;
    bool success = false;
};

class MaterialCenterReadContext final {
public:
    ~MaterialCenterReadContext()
    {
        queryService.reset();
        searchEngine.reset();
        semanticIndex.reset();
        manager.closeDatabase();
    }

    bool open(const QString &databasePath, int backendGeneration, QString *errorMessage)
    {
        if (!manager.openReadOnlyDatabase(databasePath, errorMessage)) {
            return false;
        }
        semanticIndex = std::make_unique<SemanticSearchIndexService>(&manager);
        searchEngine = std::make_unique<SearchEngine>(&manager, semanticIndex.get());
        queryService = std::make_unique<MaterialCenterQueryService>(&manager, searchEngine.get());
        path = databasePath;
        generation = backendGeneration;
        return true;
    }

    QString path;
    int generation = -1;
    GlobalDatabaseManager manager;
    std::unique_ptr<SemanticSearchIndexService> semanticIndex;
    std::unique_ptr<SearchEngine> searchEngine;
    std::unique_ptr<MaterialCenterQueryService> queryService;
};

MaterialCenterReadContext *materialCenterReadContext(const QString &databasePath,
                                                     int backendGeneration,
                                                     QString *errorMessage)
{
    thread_local std::unique_ptr<MaterialCenterReadContext> context;
    if (!context || context->path != databasePath || context->generation != backendGeneration) {
        auto replacement = std::make_unique<MaterialCenterReadContext>();
        if (!replacement->open(databasePath, backendGeneration, errorMessage)) {
            return nullptr;
        }
        context = std::move(replacement);
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return context.get();
}

}

MaterialCenterViewModel::MaterialCenterViewModel(MaterialCenterQueryService *queryService,
                                                 MaterialCatalogSyncService *syncService,
                                                 SearchDocumentSyncService *searchDocumentSyncService,
                                                 VideoAnalysisService *analysisService,
                                                 ProjectService *projectService,
                                                 AppSettings *settings,
                                                 LocalSearchAssistantRuntime *localSearchAssistantRuntime,
                                                 SearchAssistantClient *searchAssistantClient,
                                                 QObject *parent)
    : QObject(parent)
    , m_queryService(queryService)
    , m_syncService(syncService)
    , m_searchDocumentSyncService(searchDocumentSyncService)
    , m_analysisService(analysisService)
    , m_projectService(projectService)
    , m_settings(settings)
    , m_localSearchAssistantRuntime(localSearchAssistantRuntime)
    , m_searchAssistantClient(searchAssistantClient)
    , m_model(new MaterialCenterListModel(this))
    , m_folderModel(new MaterialCenterFolderListModel(this))
    , m_frameModel(new MaterialCenterFrameListModel(this))
    , m_detailRefreshTimer(new QTimer(this))
    , m_contactSheetBuildTimer(new QTimer(this))
    , m_searchRefreshTimer(new QTimer(this))
{
    m_queryPool.setMaxThreadCount(1);
    m_queryPool.setExpiryTimeout(-1);
    m_detailPool.setMaxThreadCount(1);
    m_detailPool.setExpiryTimeout(-1);
    m_detailRefreshTimer->setSingleShot(true);
    m_detailRefreshTimer->setInterval(60);
    connect(m_detailRefreshTimer, &QTimer::timeout, this, &MaterialCenterViewModel::loadPendingDetail);

    m_contactSheetBuildTimer->setSingleShot(true);
    m_contactSheetBuildTimer->setInterval(120);
    connect(m_contactSheetBuildTimer, &QTimer::timeout, this, &MaterialCenterViewModel::buildPendingContactSheet);

    m_searchRefreshTimer->setSingleShot(true);
    // Keep local results responsive while avoiding one model request per
    // keystroke. Every stable non-empty query is still sent to the local text
    // assistant after this short debounce.
    m_searchRefreshTimer->setInterval(400);
    connect(m_searchRefreshTimer, &QTimer::timeout, this, &MaterialCenterViewModel::reload);

    if (m_localSearchAssistantRuntime) {
        connect(m_localSearchAssistantRuntime,
                &LocalSearchAssistantRuntime::ready,
                this,
                [this]() {
            if (m_searchText.simplified().isEmpty()) {
                return;
            }
            startSearchUnderstanding(m_lastParsedQuery);
        });
        connect(m_localSearchAssistantRuntime,
                &LocalSearchAssistantRuntime::failed,
                this,
                [this](const QString &errorMessage) {
            m_searchAssistantBusy = !m_searchUnderstandingInFlight.isEmpty();
            m_searchAssistantUsed = false;
            m_searchAssistantStatusText = QStringLiteral("内置查询助手不可用，已保留本地搜索：%1")
                .arg(errorMessage);
            emit searchStateChanged();
        });
    }

    if (m_searchDocumentSyncService) {
        connect(m_searchDocumentSyncService,
                &SearchDocumentSyncService::synchronizationProgress,
                this,
                [this](int processed, int total, const QString &detail) {
                    m_semanticIndexing = true;
                    m_semanticIndexProcessed = qMax(0, processed);
                    m_semanticIndexTotal = qMax(0, total);
                    m_semanticIndexStatusText = detail.trimmed().isEmpty()
                        ? QStringLiteral("正在更新语义索引")
                        : detail.trimmed();
                    emit searchStateChanged();
                });
        connect(m_searchDocumentSyncService,
                &SearchDocumentSyncService::synchronizationFinished,
                this,
                [this](bool success,
                       int inserted,
                       int updated,
                       int unchanged,
                       int removed,
                       const QString &message) {
                    m_semanticIndexing = false;
                    if (success) {
                        ++m_queryBackendGeneration;
                        m_semanticIndexProcessed = m_semanticIndexTotal;
                        m_semanticIndexStatusText = QStringLiteral("语义索引已更新：新增 %1、更新 %2、未变 %3、移除 %4")
                                                        .arg(inserted)
                                                        .arg(updated)
                                                        .arg(unchanged)
                                                        .arg(removed);
                        if (hasActiveSearch()) {
                            m_searchRefreshTimer->start(0);
                        }
                    } else {
                        m_semanticIndexStatusText = QStringLiteral("语义索引更新失败：%1")
                                                        .arg(message.trimmed().isEmpty()
                                                                 ? QStringLiteral("未知错误")
                                                                 : message.trimmed());
                    }
                    emit searchStateChanged();
                });
    }

    if (m_syncService) {
        connect(m_syncService, &MaterialCatalogSyncService::catalogChanged, this, [this]() {
            ++m_queryBackendGeneration;
            ++m_detailRequestGeneration;
            m_pendingDetailVideoKey.clear();
            m_pendingContactSheetVideoKey.clear();
            m_detailRefreshTimer->stop();
            m_contactSheetBuildTimer->stop();
            reload();
        });
    }
    if (m_analysisService) {
        connect(m_analysisService, &VideoAnalysisService::catalogChanged, this, [this]() {
            ++m_queryBackendGeneration;
            ++m_detailRequestGeneration;
            m_pendingDetailVideoKey.clear();
            m_pendingContactSheetVideoKey.clear();
            m_detailRefreshTimer->stop();
            m_contactSheetBuildTimer->stop();
            reload();
        });
        connect(m_analysisService, &VideoAnalysisService::analysisProgressChanged, this,
                [this](const QString &videoKey, qint64 progress, const QString &detail, int state, const QString &errorMessage) {
            AnalysisProgressState progressState;
            progressState.progress = progress;
            progressState.detail = detail;
            progressState.errorMessage = errorMessage;
            progressState.state = static_cast<JobState>(state);
            m_analysisProgressByVideoKey.insert(videoKey, progressState);
            if (videoKey == m_detail.asset.videoKey) {
                emit analysisProgressChanged();
                emit dimensionAnalysisChanged();
            }
            if (state == static_cast<int>(JobState::Failed) && !errorMessage.trimmed().isEmpty()) {
                setMessage(errorMessage);
            } else if (state == static_cast<int>(JobState::Completed)) {
                setMessage(detail);
            }
        });
        connect(m_analysisService, &VideoAnalysisService::analysisQueueChanged, this,
                [this](const QString &currentVideoKey, int queuedCount) {
            m_currentAnalysisVideoKey = currentVideoKey;
            m_queuedAnalysisCount = queuedCount;
            emit analysisProgressChanged();
            emit statusChanged();
        });
        connect(m_analysisService, &VideoAnalysisService::dimensionAnalysisProgressChanged, this,
                [this](const QString &videoKey, bool running, const QString &detail, const QString &errorMessage) {
            DimensionProgressState state;
            state.running = running;
            state.detail = detail;
            state.errorMessage = errorMessage;
            m_dimensionProgressByVideoKey.insert(videoKey, state);
            if (videoKey == m_detail.asset.videoKey) {
                emit dimensionAnalysisChanged();
            }
            if (!errorMessage.trimmed().isEmpty()) {
                setMessage(errorMessage);
            } else if (!detail.trimmed().isEmpty()) {
                setMessage(detail);
            }
        });
    }
}

MaterialCenterViewModel::~MaterialCenterViewModel()
{
    m_queryPool.clear();
    m_detailPool.clear();
    m_queryPool.waitForDone();
    m_detailPool.waitForDone();
}

MaterialCenterListModel *MaterialCenterViewModel::model() const
{
    return m_model;
}

MaterialCenterFolderListModel *MaterialCenterViewModel::folderModel() const
{
    return m_folderModel;
}

MaterialCenterFrameListModel *MaterialCenterViewModel::frameModel() const
{
    return m_frameModel;
}

QString MaterialCenterViewModel::statusText() const
{
    int readyCount = 0;
    for (const auto &asset : m_assets) {
        if (asset.analysisStatus == VideoAnalysisStatus::Ready) {
            ++readyCount;
        }
    }
    return QStringLiteral("当前结果 %1 个文件夹 · %2 条素材 · %3 个视觉帧 · 已解析 %4 条")
        .arg(m_folders.size())
        .arg(m_assets.size())
        .arg(m_frames.size())
        .arg(readyCount);
}

QString MaterialCenterViewModel::message() const
{
    return m_message;
}

int MaterialCenterViewModel::folderCount() const
{
    return m_folders.size();
}

int MaterialCenterViewModel::assetCount() const
{
    return m_assets.size();
}

int MaterialCenterViewModel::frameCount() const
{
    return m_frames.size();
}

bool MaterialCenterViewModel::frameSearchMode() const
{
    return (hasActiveSearch()
            || m_searchResultFilter == static_cast<int>(SearchResultQuickFilter::Frames))
        && m_lastParsedQuery.resultTarget == SearchResultTarget::Frames;
}

bool MaterialCenterViewModel::hasActiveSearch() const
{
    return !m_searchText.trimmed().isEmpty();
}

bool MaterialCenterViewModel::semanticSearchAvailable() const
{
    return m_semanticSearchAvailable;
}

QString MaterialCenterViewModel::semanticSearchStatusText() const
{
    if (!hasActiveSearch()) {
        return {};
    }
    if (m_lastParsedQuery.semanticText.trimmed().isEmpty()) {
        return QStringLiteral("已按日期、类型与结果目标执行结构化检索");
    }
    return m_semanticSearchAvailable
        ? QStringLiteral("语义检索已启用")
        : QStringLiteral("语义检索不可用，当前使用词法、路径与结构化筛选");
}

bool MaterialCenterViewModel::semanticIndexing() const
{
    return m_semanticIndexing;
}

int MaterialCenterViewModel::semanticIndexProgress() const
{
    if (m_semanticIndexTotal <= 0) {
        return 0;
    }
    return qBound(0,
                  qRound((100.0 * m_semanticIndexProcessed) / m_semanticIndexTotal),
                  100);
}

QString MaterialCenterViewModel::semanticIndexStatusText() const
{
    if (m_semanticIndexing && m_semanticIndexTotal > 0) {
        return QStringLiteral("%1（%2 / %3）")
            .arg(m_semanticIndexStatusText)
            .arg(m_semanticIndexProcessed)
            .arg(m_semanticIndexTotal);
    }
    return m_semanticIndexStatusText;
}

QString MaterialCenterViewModel::searchAssistantStatusText() const
{
    return hasActiveSearch() ? m_searchAssistantStatusText : QString();
}

bool MaterialCenterViewModel::searchAssistantBusy() const
{
    return hasActiveSearch() && m_searchAssistantBusy;
}

bool MaterialCenterViewModel::searchAssistantUsed() const
{
    return hasActiveSearch() && m_searchAssistantUsed;
}

QString MaterialCenterViewModel::searchWarningMessage() const
{
    return m_searchWarningMessage;
}

QString MaterialCenterViewModel::searchInterpretationText() const
{
    return hasActiveSearch() ? m_searchInterpretationText : QString();
}

QString MaterialCenterViewModel::searchEmptyReason() const
{
    return hasActiveSearch() ? m_searchEmptyReason : QString();
}

int MaterialCenterViewModel::excludedPartialCount() const
{
    return m_excludedPartialCount;
}

QVariantList MaterialCenterViewModel::projectOptions() const
{
    return m_projectOptions;
}

QVariantList MaterialCenterViewModel::sourceOptions() const
{
    return m_sourceOptions;
}

QVariantList MaterialCenterViewModel::assetTypeOptions() const
{
    return m_assetTypeOptions;
}

QString MaterialCenterViewModel::projectFilter() const
{
    return m_projectFilter;
}

QString MaterialCenterViewModel::sourceFilter() const
{
    return m_sourceFilter;
}

int MaterialCenterViewModel::assetTypeFilter() const
{
    return m_assetTypeFilter;
}

int MaterialCenterViewModel::searchResultFilter() const
{
    return m_searchResultFilter;
}

int MaterialCenterViewModel::analysisStatusFilter() const
{
    return m_analysisStatusFilter;
}

QVariantList MaterialCenterViewModel::analysisStatusOptions() const
{
    return {
        QVariantMap{{QStringLiteral("value"), -1}, {QStringLiteral("label"), QStringLiteral("全部状态")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(VideoAnalysisStatus::Pending)}, {QStringLiteral("label"), QStringLiteral("待解析")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(VideoAnalysisStatus::Running)}, {QStringLiteral("label"), QStringLiteral("解析中")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(VideoAnalysisStatus::Ready)}, {QStringLiteral("label"), QStringLiteral("已解析")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(VideoAnalysisStatus::Failed)}, {QStringLiteral("label"), QStringLiteral("解析失败")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(VideoAnalysisStatus::IndexedOnly)}, {QStringLiteral("label"), QStringLiteral("仅索引")}}
    };
}

QString MaterialCenterViewModel::selectedVideoKey() const
{
    return m_detail.asset.videoKey;
}

QString MaterialCenterViewModel::selectedAssetKey() const
{
    return m_detail.asset.assetKey.trimmed().isEmpty() ? m_detail.asset.videoKey : m_detail.asset.assetKey;
}

int MaterialCenterViewModel::selectedVideoIndex() const
{
    const auto selectedKey = m_detail.asset.videoKey;
    if (selectedKey.trimmed().isEmpty()) {
        return -1;
    }
    for (int index = 0; index < m_assets.size(); ++index) {
        if (m_assets.at(index).videoKey == selectedKey) {
            return index;
        }
    }
    return -1;
}

QString MaterialCenterViewModel::quickSearchRevealVideoKey() const
{
    return m_quickSearchRevealVideoKey;
}

int MaterialCenterViewModel::quickSearchRevealFrameNumber() const
{
    return m_quickSearchRevealFrameNumber;
}

int MaterialCenterViewModel::quickSearchRevealIndex() const
{
    const auto targetKey = m_quickSearchRevealVideoKey.trimmed();
    if (targetKey.isEmpty()) {
        return -1;
    }
    if (frameSearchMode()) {
        for (int index = 0; index < m_frames.size(); ++index) {
            const auto &frame = m_frames.at(index);
            if (frame.videoKey == targetKey
                && (m_quickSearchRevealFrameNumber < 0
                    || frame.frameNumber == m_quickSearchRevealFrameNumber)) {
                return index;
            }
        }
        return -1;
    }
    for (int index = 0; index < m_assets.size(); ++index) {
        if (m_assets.at(index).videoKey == targetKey) {
            return index;
        }
    }
    return -1;
}

int MaterialCenterViewModel::quickSearchRevealRevision() const
{
    return m_quickSearchRevealRevision;
}

bool MaterialCenterViewModel::hasSelection() const
{
    return !m_detail.asset.videoKey.trimmed().isEmpty();
}

QString MaterialCenterViewModel::selectedTitle() const
{
    return m_detail.asset.fileName;
}

QString MaterialCenterViewModel::selectedProjectName() const
{
    return m_detail.asset.projectName;
}

QString MaterialCenterViewModel::selectedSourceName() const
{
    return m_detail.asset.sourceRootName;
}

QString MaterialCenterViewModel::selectedAssetTypeLabel() const
{
    return Formatters::assetTypeLabel(m_detail.asset.assetType);
}

QString MaterialCenterViewModel::selectedExtension() const
{
    return m_detail.asset.extension;
}

QString MaterialCenterViewModel::selectedTechnicalSummary() const
{
    return m_detail.asset.technicalSummary;
}

QString MaterialCenterViewModel::selectedSourceTextPreview() const
{
    return previewText(m_detail.asset.sourceText);
}

QString MaterialCenterViewModel::selectedSummary() const
{
    return m_detail.asset.summary;
}

QVariantList MaterialCenterViewModel::selectedKeywords() const
{
    return stringListToVariants(m_detail.asset.keywords);
}

QVariantList MaterialCenterViewModel::selectedScenes() const
{
    return stringListToVariants(m_detail.asset.scenes);
}

QVariantList MaterialCenterViewModel::selectedDimensionAnalyses() const
{
    return dimensionAnalysesToVariants(m_detail.dimensionAnalyses);
}

QVariantList MaterialCenterViewModel::selectedFrames() const
{
    return m_selectedFramesCache;
}

QString MaterialCenterViewModel::selectedFrameSamplingText() const
{
    if (!selectedIsVideo() || !m_detail.hasVisualAnalysisPlan) {
        return {};
    }

    const auto &plan = m_detail.visualAnalysisPlan;
    const auto intervalSeconds = qMax(1, plan.frameInterval) / 1000.0;
    QString modeText = QStringLiteral("候选帧采样");
    if (plan.samplingPolicy.contains(QStringLiteral("strategy=0"))) {
        modeText = QStringLiteral("逐帧候选");
    } else if (plan.samplingPolicy.contains(QStringLiteral("strategy=1"))) {
        modeText = QStringLiteral("场景变化 + %1 秒间隔").arg(intervalSeconds, 0, 'f', 2);
    } else if (plan.samplingPolicy.contains(QStringLiteral("strategy=2"))) {
        modeText = QStringLiteral("每 %1 秒取样").arg(intervalSeconds, 0, 'f', 2);
    } else if (plan.samplingPolicy.contains(QStringLiteral("strategy=3"))) {
        modeText = QStringLiteral("高保真 %1 秒取样").arg(intervalSeconds, 0, 'f', 2);
    }
    const auto terminalCovered = VisualAnalysisMetadata::isCurrentSamplingPolicy(plan.samplingPolicy)
        && plan.plannedFrameCount > 0
        && m_selectedTotalFrameCount == plan.plannedFrameCount;
    const auto coverageText = terminalCovered
        ? QStringLiteral("首尾时间锚点已纳入计划")
        : QStringLiteral("旧采样计划需重新解析");
    return QStringLiteral("当前结果：%1 · %2 · 共 %3 帧")
        .arg(modeText, coverageText)
        .arg(m_selectedTotalFrameCount);
}

QString MaterialCenterViewModel::selectedFrameSearchStatus() const
{
    return m_selectedFrameSearchStatusCache;
}

int MaterialCenterViewModel::selectedFrameCount() const
{
    return m_selectedTotalFrameCount;
}

int MaterialCenterViewModel::selectedVisibleFrameCount() const
{
    return m_selectedFramesCache.size();
}

int MaterialCenterViewModel::selectedRemainingFrameCount() const
{
    return qMax(0, selectedFrameCount() - m_detail.frames.size());
}

bool MaterialCenterViewModel::selectedFramesExpanded() const
{
    return selectedFrameCount() > 0 && !m_selectedFramesHasMore;
}

bool MaterialCenterViewModel::canExpandSelectedFrames() const
{
    return m_selectedFramesHasMore;
}

bool MaterialCenterViewModel::canLoadMoreSelectedFrames() const
{
    return m_selectedFramesHasMore && !m_selectedFramesLoading;
}

bool MaterialCenterViewModel::selectedFramesLoading() const
{
    return m_selectedFramesLoading;
}

bool MaterialCenterViewModel::selectedThumbnailLoading() const
{
    return hasSelection()
        && m_detail.asset.thumbnailStatus == ThumbnailStatus::Running
        && m_selectedThumbnailUrlCache.isEmpty();
}

QUrl MaterialCenterViewModel::selectedThumbnailUrl() const
{
    return m_selectedThumbnailUrlCache;
}

QString MaterialCenterViewModel::selectedFilePath() const
{
    return m_detail.asset.absolutePath;
}

QString MaterialCenterViewModel::selectedCachePath() const
{
    return hasSelection()
        ? Paths::projectFrameCacheDirectory(m_detail.asset.projectDatabasePath, m_detail.asset.videoKey)
        : QString();
}

QString MaterialCenterViewModel::selectedAnalysisStatusLabel() const
{
    return Formatters::videoAnalysisStatusLabel(m_detail.asset.analysisStatus, m_detail.asset.confirmationStatus);
}

bool MaterialCenterViewModel::selectedAnalysisBusy() const
{
    const auto state = selectedProgressState().state;
    return state == JobState::Pending || state == JobState::Running;
}

int MaterialCenterViewModel::selectedAnalysisProgress() const
{
    const auto state = selectedProgressState();
    if (!state.detail.trimmed().isEmpty() || state.state == JobState::Pending || state.state == JobState::Running) {
        return static_cast<int>(qBound<qint64>(0LL, state.progress, 100LL));
    }
    return persistedAnalysisProgress(m_detail.asset);
}

QString MaterialCenterViewModel::selectedAnalysisProgressText() const
{
    const auto state = selectedProgressState();
    if (!state.detail.trimmed().isEmpty()) {
        return state.detail;
    }
    if (m_detail.asset.analysisStatus == VideoAnalysisStatus::Failed
        || m_detail.asset.analysisStatus == VideoAnalysisStatus::Running) {
        return failedAnalysisHint(m_detail.asset);
    }
    if (m_detail.asset.analysisStatus == VideoAnalysisStatus::Ready) {
        return readyAnalysisHint(m_detail.asset);
    }
    if (m_detail.asset.analysisStatus == VideoAnalysisStatus::IndexedOnly) {
        return QStringLiteral("该素材类型仅进入索引，不需要内容解析。");
    }
    return hasSelection() ? QStringLiteral("尚未开始解析。") : QString();
}

QString MaterialCenterViewModel::selectedAnalysisError() const
{
    const auto state = selectedProgressState();
    if (!state.errorMessage.trimmed().isEmpty()) {
        return state.errorMessage;
    }
    if (!m_detail.asset.errorMessage.trimmed().isEmpty()) {
        return m_detail.asset.errorMessage;
    }
    return m_detail.asset.analysisStatus == VideoAnalysisStatus::Failed
            || m_detail.asset.analysisStatus == VideoAnalysisStatus::Running
        ? m_detail.asset.analysisTask.lastErrorMessage
        : QString();
}

QString MaterialCenterViewModel::analyzeButtonText() const
{
    const auto state = selectedProgressState().state;
    if (state == JobState::Pending) {
        return QStringLiteral("排队中");
    }
    if (state == JobState::Running) {
        return QStringLiteral("解析中");
    }
    if (!canAnalyzeAsset(m_detail.asset)) {
        return QStringLiteral("仅索引");
    }
    if (m_detail.asset.analysisStatus == VideoAnalysisStatus::Failed
        || m_detail.asset.analysisStatus == VideoAnalysisStatus::Running) {
        return QStringLiteral("继续解析");
    }
    if (m_detail.asset.analysisStatus == VideoAnalysisStatus::Ready) {
        return QStringLiteral("重新解析");
    }
    return QStringLiteral("开始解析");
}

bool MaterialCenterViewModel::canAnalyzeSelected() const
{
    return hasSelection() && canAnalyzeAsset(m_detail.asset) && !selectedAnalysisBusy();
}

bool MaterialCenterViewModel::selectedDimensionAnalysisBusy() const
{
    return selectedDimensionProgressState().running;
}

QString MaterialCenterViewModel::selectedDimensionAnalysisText() const
{
    return selectedDimensionProgressState().detail;
}

QString MaterialCenterViewModel::selectedDimensionAnalysisError() const
{
    return selectedDimensionProgressState().errorMessage;
}

bool MaterialCenterViewModel::selectedIsVideo() const
{
    return hasSelection() && m_detail.asset.assetType == AssetType::Video;
}

int MaterialCenterViewModel::queuedAnalysisCount() const
{
    return m_queuedAnalysisCount;
}

bool MaterialCenterViewModel::hasAnalyzedVisible() const
{
    for (const auto &asset : m_assets) {
        const bool hasExistingAnalysis = asset.analysisStatus == VideoAnalysisStatus::Ready
            || asset.analysisStatus == VideoAnalysisStatus::Failed
            || asset.analysisStatus == VideoAnalysisStatus::Running;
        if (hasExistingAnalysis && canAnalyzeAsset(asset)) {
            return true;
        }
    }
    return false;
}

void MaterialCenterViewModel::reload()
{
    if (!m_queryService) {
        return;
    }

    m_searchRefreshTimer->stop();
    ++m_searchGeneration;
    m_assetTypeOptions = fileTypeOptions();
    executeSearch();
}

void MaterialCenterViewModel::prepareGlobalQuickSearch()
{
    emit searchAssistantWarmupRequested();
    m_searchRefreshTimer->stop();
    ++m_searchGeneration;
    m_searchText.clear();
    m_projectFilter.clear();
    m_sourceFilter.clear();
    m_assetTypeFilter = -1;
    m_searchResultFilter = static_cast<int>(SearchResultQuickFilter::Smart);
    m_analysisStatusFilter = -1;
    m_searchAssistantBusy = false;
    m_searchAssistantUsed = false;
    m_searchAssistantStatusText.clear();
    m_searchWarningMessage.clear();
    m_searchInterpretationText.clear();
    m_searchEmptyReason.clear();
    m_lastParsedQuery = {};
    m_quickSearchRevealVideoKey.clear();
    m_quickSearchRevealFrameNumber = -1;
    ++m_quickSearchRevealRevision;
    emit quickSearchRevealChanged();
    reload();
}

void MaterialCenterViewModel::executeSearch(const ModelSearchUnderstanding *modelUnderstanding)
{
    if (!m_queryService) {
        return;
    }

    MaterialSearchScope scope;
    scope.projectUuid = m_projectFilter;
    scope.sourceRootName = m_sourceFilter;
    scope.analysisStatusFilter = m_analysisStatusFilter;
    scope.confirmationStatusFilter = -1;
    scope.assetTypeFilter = m_assetTypeFilter;
    scope.resultQuickFilter = static_cast<SearchResultQuickFilter>(m_searchResultFilter);
    const auto logicalGeneration = m_searchGeneration;
    const auto requestGeneration = ++m_searchRequestGeneration;
    const auto backendGeneration = m_queryBackendGeneration;
    const auto databasePath = m_queryService->databaseFilePath();
    const auto queryText = m_searchText;
    const auto projectFilter = m_projectFilter;
    const auto referenceDate = QDate::currentDate();
    const auto understanding = modelUnderstanding
        ? std::optional<ModelSearchUnderstanding>(*modelUnderstanding)
        : std::nullopt;

    auto *watcher = new QFutureWatcher<MaterialCenterSearchTaskResult>(this);
    connect(watcher, &QFutureWatcher<MaterialCenterSearchTaskResult>::finished, this,
            [this, watcher]() {
        auto task = watcher->result();
        watcher->deleteLater();
        if (task.logicalGeneration != m_searchGeneration
            || task.requestGeneration != m_searchRequestGeneration) {
            return;
        }
        if (!task.errorMessage.isEmpty()) {
            m_searchWarningMessage = task.errorMessage;
            emit searchStateChanged();
            return;
        }
        if (!task.enhanced) {
            m_projectOptions = prependAllOption(task.projectOptions, QStringLiteral("全部项目"));
            m_sourceOptions = prependAllOption(task.sourceOptions, QStringLiteral("全部素材源"));
            emit filtersChanged();
            m_lastBaselineSearchResult = task.result;
            m_lastBaselineSearchGeneration = task.logicalGeneration;
        } else if (m_lastBaselineSearchGeneration == task.logicalGeneration
                   && m_lastBaselineSearchResult.parsedQuery.originalText.simplified()
                       == m_searchText.simplified()) {
            auto outcome = SearchResultFusion::preserveBaselineRecall(
                m_lastBaselineSearchResult,
                task.result);
            task.result = std::move(outcome.result);
            switch (outcome.protection) {
            case SearchRecallProtection::EnhancedResultEmpty:
                m_searchAssistantUsed = false;
                m_searchAssistantStatusText = QStringLiteral(
                    "模型条件过窄，已保留 %1 条原向量/本地召回结果")
                                                  .arg(outcome.preservedHitCount);
                break;
            case SearchRecallProtection::ResultTargetChanged:
                m_searchAssistantUsed = false;
                m_searchAssistantStatusText = QStringLiteral(
                    "模型改变了结果类型，已保留 %1 条原向量/本地召回结果")
                                                  .arg(outcome.preservedHitCount);
                break;
            case SearchRecallProtection::BaselineHitsAdded:
                m_searchAssistantUsed = true;
                m_searchAssistantStatusText = QStringLiteral(
                    "模型增强：新增 %1 条、双路共同命中 %2 条，并保留 %3 条原本地召回")
                                                  .arg(outcome.enhancedOnlyHitCount)
                                                  .arg(outcome.sharedHitCount)
                                                  .arg(outcome.preservedHitCount);
                break;
            case SearchRecallProtection::None:
                break;
            }
        }
        applySearchResult(task.result);
        if (!task.enhanced) {
            startSearchUnderstanding(task.result.parsedQuery);
        }
    });
    watcher->setFuture(QtConcurrent::run(
        &m_queryPool,
        [databasePath,
         backendGeneration,
         logicalGeneration,
         requestGeneration,
         queryText,
         projectFilter,
         scope,
         referenceDate,
         understanding]() {
            MaterialCenterSearchTaskResult task;
            task.logicalGeneration = logicalGeneration;
            task.requestGeneration = requestGeneration;
            task.enhanced = understanding.has_value();
            auto *context = materialCenterReadContext(
                databasePath, backendGeneration, &task.errorMessage);
            if (!context || !context->queryService) {
                return task;
            }
            if (!task.enhanced) {
                task.projectOptions = context->queryService->fetchProjectOptions();
                task.sourceOptions = context->queryService->fetchSourceOptions(projectFilter);
            }
            task.result = context->queryService->searchMaterials(
                queryText,
                scope,
                referenceDate,
                understanding ? &*understanding : nullptr);
            return task;
        }));
}

void MaterialCenterViewModel::applySearchResult(const MaterialSearchResult &result)
{
    m_folders = result.folders;
    m_assets = result.assets;
    m_frames = result.frames;
    m_semanticSearchAvailable = result.semanticSearchAvailable;
    m_searchWarningMessage = result.warningMessage;
    m_searchInterpretationText = result.parsedQuery.interpretationLabels.join(QStringLiteral(" · "));
    m_lastParsedQuery = result.parsedQuery;
    m_lastSearchReliability = result.reliability;
    m_excludedPartialCount = result.excludedPartialCount;
    m_searchEmptyReason.clear();
    if (hasActiveSearch() && m_folders.isEmpty() && m_assets.isEmpty() && m_frames.isEmpty()) {
        if (result.parsedQuery.resultTarget == SearchResultTarget::Folders) {
            m_searchEmptyReason = QStringLiteral("没有找到符合这些条件的文件夹。请检查目录日期、项目范围，或改为搜索素材所在文件夹。");
        } else if (result.parsedQuery.resultTarget == SearchResultTarget::Frames) {
            m_searchEmptyReason = QStringLiteral("没有找到符合条件的视觉帧。请确认素材已完成逐帧视觉解析，或放宽颜色、材质和对象条件。");
        } else {
            m_searchEmptyReason = QStringLiteral("没有找到符合这些条件的素材。请检查拍摄日期和类型；画面内容查询还需要素材完成视觉解析。 ");
        }
        m_searchEmptyReason = m_searchEmptyReason.trimmed();
    }
    m_folderModel->setItems(m_folders);
    m_model->setItems(m_assets);
    m_frameModel->setItems(m_frames);

    const auto revealKey = m_quickSearchRevealVideoKey.trimmed();
    const bool revealAssetVisible = std::any_of(m_assets.cbegin(),
                                                m_assets.cend(),
                                                [&revealKey](const auto &asset) {
                                                    return asset.videoKey == revealKey;
                                                });
    const bool revealFrameVisible = std::any_of(m_frames.cbegin(),
                                                m_frames.cend(),
                                                [this, &revealKey](const auto &frame) {
                                                    return frame.videoKey == revealKey
                                                        && (m_quickSearchRevealFrameNumber < 0
                                                            || frame.frameNumber == m_quickSearchRevealFrameNumber);
                                                });
    QString selectedKey = (revealAssetVisible || revealFrameVisible)
        ? revealKey
        : m_detail.asset.videoKey.trimmed();
    const bool selectedFrameVisible = std::any_of(m_frames.cbegin(),
                                                  m_frames.cend(),
                                                  [&selectedKey](const auto &frame) {
                                                      return frame.videoKey == selectedKey;
                                                  });
    if (!selectedKey.isEmpty()
        && assetByVideoKey(selectedKey).videoKey.trimmed().isEmpty()
        && !selectedFrameVisible) {
        selectedKey.clear();
    }
    if (selectedKey.isEmpty() && !m_assets.isEmpty()) {
        selectedKey = m_assets.first().videoKey;
    }
    prepareSelection(selectedKey);
    emit filtersChanged();
    emit searchStateChanged();
    emit statusChanged();
    emit selectionChanged();
    emit analysisProgressChanged();
    emit dimensionAnalysisChanged();
}

QString MaterialCenterViewModel::searchUnderstandingCacheKey(const QString &queryText,
                                                              const QDate &referenceDate) const
{
    const auto modelIdentity = m_localSearchAssistantRuntime
        ? m_localSearchAssistantRuntime->modelName().toCaseFolded()
        : QStringLiteral("builtin-unavailable");
    return QStringLiteral("v6|%1|%2|%3")
        .arg(referenceDate.toString(Qt::ISODate),
             modelIdentity,
             queryText.simplified().toCaseFolded());
}

void MaterialCenterViewModel::startSearchUnderstanding(
    const ParsedMaterialQuery &localQuery)
{
    const auto queryText = m_searchText.simplified();
    if (queryText.isEmpty()) {
        m_searchAssistantStatusText.clear();
        m_searchAssistantBusy = false;
        m_searchAssistantUsed = false;
        emit searchStateChanged();
        return;
    }
    if (m_settings && !m_settings->searchAssistantEnabled()) {
        m_searchAssistantStatusText = QStringLiteral("内置文本查询助手已关闭，当前使用本地规则与混合检索");
        m_searchAssistantBusy = false;
        m_searchAssistantUsed = false;
        emit searchStateChanged();
        return;
    }
    if (localQuery.semanticText.trimmed().isEmpty()
        && localQuery.strictEntities.isEmpty()
        && localQuery.explicitEntityLabels.isEmpty()
        && localQuery.ocrText.trimmed().isEmpty()) {
        m_searchAssistantStatusText = QStringLiteral("本地规则已完整理解日期、类型与目标，无需调用查询助手");
        m_searchAssistantBusy = false;
        m_searchAssistantUsed = false;
        emit searchStateChanged();
        return;
    }

    const bool assistantReady = m_localSearchAssistantRuntime
        && m_localSearchAssistantRuntime->isReady();
    if (m_lastBaselineSearchGeneration == m_searchGeneration
        && m_lastBaselineSearchResult.parsedQuery.originalText.simplified() == queryText) {
        const auto routing = SearchAssistantRouter::decide(
            m_lastBaselineSearchResult,
            assistantReady);
        if (!routing.shouldInvoke(assistantReady)) {
            m_searchAssistantStatusText = routing.reasons.isEmpty()
                ? QStringLiteral("本地搜索结果可靠，无需调用查询助手")
                : routing.reasons.join(QStringLiteral("；"));
            m_searchAssistantBusy = false;
            m_searchAssistantUsed = false;
            emit searchStateChanged();
            return;
        }
    }
    if (!m_localSearchAssistantRuntime || !m_searchAssistantClient) {
        m_searchAssistantStatusText = QStringLiteral("内置查询助手未初始化，已使用本地查询理解");
        m_searchAssistantBusy = false;
        m_searchAssistantUsed = false;
        emit searchStateChanged();
        return;
    }

    const auto referenceDate = QDate::currentDate();
    const auto cacheKey = searchUnderstandingCacheKey(queryText, referenceDate);
    const auto now = QDateTime::currentDateTimeUtc();
    auto cached = m_searchUnderstandingCache.find(cacheKey);
    if (cached != m_searchUnderstandingCache.end() && cached->expiresAt <= now) {
        m_searchUnderstandingCacheOrder.removeAll(cacheKey);
        m_searchUnderstandingCache.erase(cached);
        cached = m_searchUnderstandingCache.end();
    }
    if (cached != m_searchUnderstandingCache.end()) {
        m_searchUnderstandingCacheOrder.removeAll(cacheKey);
        m_searchUnderstandingCacheOrder.append(cacheKey);
        bool applied = false;
        SearchQueryUnderstanding::merge(localQuery, cached->understanding, &applied);
        if (applied) {
            m_searchAssistantStatusText = QStringLiteral("内置文本模型已辅助理解（缓存）");
            m_searchAssistantUsed = true;
            executeSearch(&cached->understanding);
        } else {
            m_searchAssistantStatusText = QStringLiteral("模型未发现需要补充的条件，已使用本地理解");
            m_searchAssistantUsed = false;
        }
        m_searchAssistantBusy = false;
        emit searchStateChanged();
        return;
    }
    const auto recentFailure = m_searchUnderstandingFailures.constFind(cacheKey);
    if (recentFailure != m_searchUnderstandingFailures.cend()) {
        if (recentFailure.value() > now) {
            m_searchAssistantStatusText = QStringLiteral(
                "当前查询近期模型增强失败，暂时保留本地搜索");
            m_searchAssistantBusy = false;
            m_searchAssistantUsed = false;
            emit searchStateChanged();
            return;
        }
        m_searchUnderstandingFailures.remove(cacheKey);
    }
    if (m_searchUnderstandingInFlight.contains(cacheKey)) {
        m_searchAssistantStatusText = QStringLiteral("内置文本模型正在理解当前搜索…");
        m_searchAssistantBusy = true;
        emit searchStateChanged();
        return;
    }
    if (!m_localSearchAssistantRuntime->isReady()) {
        const auto started = m_localSearchAssistantRuntime->start();
        m_searchAssistantStatusText = started
            ? QStringLiteral("正在按需启动内置文本查询助手…")
            : QStringLiteral("内置查询助手不可用，已保留本地搜索：%1")
                  .arg(m_localSearchAssistantRuntime->lastError());
        m_searchAssistantBusy = started;
        m_searchAssistantUsed = false;
        emit searchStateChanged();
        return;
    }
    const auto baseUrl = m_localSearchAssistantRuntime->endpoint();
    const auto model = m_localSearchAssistantRuntime->modelName();
    const auto timeoutSec = 3;
    auto *client = m_searchAssistantClient;
    auto *watcher = new QFutureWatcher<SearchUnderstandingTaskResult>(this);
    m_searchUnderstandingInFlight.insert(cacheKey);
    m_searchAssistantStatusText = QStringLiteral("内置文本模型正在理解当前搜索…");
    m_searchAssistantBusy = true;
    m_searchAssistantUsed = false;
    emit searchStateChanged();

    connect(watcher, &QFutureWatcher<SearchUnderstandingTaskResult>::finished, this,
            [this, watcher, localQuery]() {
        const auto task = watcher->result();
        watcher->deleteLater();
        m_searchUnderstandingInFlight.remove(task.cacheKey);

        const auto now = QDateTime::currentDateTimeUtc();
        if (task.understanding.has_value() && task.understanding->confidence >= 0.55) {
            while (m_searchUnderstandingCache.size() >= 256
                   && !m_searchUnderstandingCacheOrder.isEmpty()) {
                m_searchUnderstandingCache.remove(m_searchUnderstandingCacheOrder.takeFirst());
            }
            m_searchUnderstandingFailures.remove(task.cacheKey);
            m_searchUnderstandingCacheOrder.removeAll(task.cacheKey);
            m_searchUnderstandingCacheOrder.append(task.cacheKey);
            m_searchUnderstandingCache.insert(
                task.cacheKey,
                SearchUnderstandingCacheEntry{
                    *task.understanding,
                    now.addDays(7)
                });
        } else if (!task.understanding.has_value()) {
            if (m_searchUnderstandingFailures.size() >= 128) {
                m_searchUnderstandingFailures.erase(m_searchUnderstandingFailures.begin());
            }
            m_searchUnderstandingFailures.insert(task.cacheKey, now.addSecs(120));
        }

        const auto currentQuery = m_searchText.simplified();
        const auto currentCacheKey = currentQuery.isEmpty()
            ? QString()
            : searchUnderstandingCacheKey(currentQuery, QDate::currentDate());
        if (task.queryText != currentQuery || task.cacheKey != currentCacheKey) {
            m_searchAssistantBusy = !currentCacheKey.isEmpty()
                && m_searchUnderstandingInFlight.contains(currentCacheKey);
            emit searchStateChanged();
            return;
        }
        m_searchAssistantBusy = false;
        if (!task.understanding.has_value()) {
            m_searchAssistantUsed = false;
            m_searchAssistantStatusText = QStringLiteral("模型理解失败，已保留本地搜索：%1")
                .arg(task.errorMessage.isEmpty() ? QStringLiteral("未知错误") : task.errorMessage);
            emit searchStateChanged();
            return;
        }
        if (task.understanding->confidence < 0.55) {
            m_searchAssistantUsed = false;
            m_searchAssistantStatusText = QStringLiteral("模型置信度不足，已使用本地查询理解");
            emit searchStateChanged();
            return;
        }
        bool applied = false;
        SearchQueryUnderstanding::merge(localQuery, *task.understanding, &applied);
        if (!applied) {
            m_searchAssistantUsed = false;
            m_searchAssistantStatusText = QStringLiteral("模型未发现需要补充的条件，已使用本地理解");
            emit searchStateChanged();
            return;
        }
        m_searchAssistantUsed = true;
        m_searchAssistantStatusText = QStringLiteral("内置文本模型已辅助理解");
        executeSearch(&*task.understanding);
    });

    watcher->setFuture(QtConcurrent::run([client,
                                          queryText,
                                          referenceDate,
                                          baseUrl,
                                          model,
                                          timeoutSec,
                                          cacheKey]() {
        SearchUnderstandingTaskResult task;
        task.cacheKey = cacheKey;
        task.queryText = queryText;
        task.understanding = client->understandQuery(queryText,
                                                     referenceDate,
                                                     baseUrl,
                                                     model,
                                                     timeoutSec,
                                                     &task.errorMessage);
        return task;
    }));
}

void MaterialCenterViewModel::setSearchText(const QString &searchText)
{
    if (m_searchText == searchText) {
        return;
    }
    m_searchText = searchText;
    ++m_searchGeneration;
    m_searchAssistantStatusText.clear();
    m_searchAssistantBusy = false;
    m_searchAssistantUsed = false;
    m_searchInterpretationText.clear();
    m_searchEmptyReason.clear();
    m_lastParsedQuery = {};
    emit searchStateChanged();
    if (!m_searchText.simplified().isEmpty()) {
        emit searchAssistantWarmupRequested();
    }
    m_searchRefreshTimer->start();
}

void MaterialCenterViewModel::setProjectFilter(const QString &projectUuid)
{
    if (m_projectFilter == projectUuid) {
        return;
    }
    m_projectFilter = projectUuid;
    m_sourceFilter.clear();
    reload();
}

void MaterialCenterViewModel::setSourceFilter(const QString &sourceName)
{
    if (m_sourceFilter == sourceName) {
        return;
    }
    m_sourceFilter = sourceName;
    reload();
}

void MaterialCenterViewModel::setAssetTypeFilter(int assetType)
{
    if (m_assetTypeFilter == assetType
        && m_searchResultFilter == static_cast<int>(SearchResultQuickFilter::Smart)) {
        return;
    }
    m_assetTypeFilter = assetType;
    m_searchResultFilter = static_cast<int>(SearchResultQuickFilter::Smart);
    reload();
}

void MaterialCenterViewModel::setSearchResultFilter(int filter)
{
    const auto normalized = qBound(
        static_cast<int>(SearchResultQuickFilter::Smart),
        filter,
        static_cast<int>(SearchResultQuickFilter::Document));
    if (m_searchResultFilter == normalized) {
        return;
    }
    m_searchResultFilter = normalized;
    switch (static_cast<SearchResultQuickFilter>(normalized)) {
    case SearchResultQuickFilter::Video:
        m_assetTypeFilter = static_cast<int>(AssetType::Video);
        break;
    case SearchResultQuickFilter::Frames:
    case SearchResultQuickFilter::Smart:
        m_assetTypeFilter = -1;
        break;
    case SearchResultQuickFilter::Image:
        m_assetTypeFilter = static_cast<int>(AssetType::Image);
        break;
    case SearchResultQuickFilter::Document:
        m_assetTypeFilter = static_cast<int>(AssetType::Document);
        break;
    }
    reload();
}

void MaterialCenterViewModel::setAnalysisStatusFilter(int status)
{
    if (m_analysisStatusFilter == status) {
        return;
    }
    m_analysisStatusFilter = status;
    reload();
}

void MaterialCenterViewModel::selectVideo(const QString &videoKey)
{
    const auto normalizedKey = videoKey.trimmed();
    if (m_detail.asset.videoKey == normalizedKey) {
        return;
    }
    prepareSelection(normalizedKey);
    emit selectionChanged();
    emit analysisProgressChanged();
    emit dimensionAnalysisChanged();
}

void MaterialCenterViewModel::selectVideoAt(int index)
{
    if (index < 0 || index >= m_assets.size()) {
        return;
    }
    selectVideo(m_assets.at(index).videoKey);
}

void MaterialCenterViewModel::moveVideoSelection(int delta)
{
    if (m_assets.isEmpty()) {
        return;
    }
    const auto currentIndex = selectedVideoIndex();
    const auto targetIndex = currentIndex < 0
        ? 0
        : qBound(0, currentIndex + delta, m_assets.size() - 1);
    selectVideoAt(targetIndex);
}

void MaterialCenterViewModel::syncCurrentProject()
{
    if (m_syncService) {
        m_syncService->syncCurrentProject();
        setMessage(QStringLiteral("已开始同步当前项目到素材管理中心。"));
    }
}

void MaterialCenterViewModel::rebuildGlobalIndex()
{
    if (m_syncService) {
        m_syncService->rebuildAllProjects();
        setMessage(QStringLiteral("已开始重建全局素材索引。"));
    }
}

void MaterialCenterViewModel::updateSemanticIndexNow()
{
    if (m_searchDocumentSyncService && !m_semanticIndexing) {
        m_searchDocumentSyncService->scheduleImmediateFullSync();
    }
}

void MaterialCenterViewModel::analyzeSelected()
{
    if (!hasSelection() || !m_analysisService) {
        return;
    }
    QString errorMessage;
    if (m_analysisService->enqueueVideo(m_detail.asset.videoKey, &errorMessage)) {
        setMessage(QStringLiteral("已加入解析队列：%1").arg(m_detail.asset.fileName));
    } else {
        setMessage(errorMessage);
        AnalysisProgressState state;
        state.progress = selectedAnalysisProgress();
        state.detail = errorMessage;
        state.errorMessage = errorMessage;
        state.state = JobState::Failed;
        m_analysisProgressByVideoKey.insert(m_detail.asset.videoKey, state);
        emit analysisProgressChanged();
    }
}

void MaterialCenterViewModel::retrySelectedFrame(int frameNumber)
{
    if (!hasSelection() || !m_analysisService) {
        return;
    }

    QString errorMessage;
    if (m_analysisService->retryFrame(m_detail.asset.videoKey, frameNumber, &errorMessage)) {
        setMessage(QStringLiteral("已加入第 %1 帧重解析队列：%2").arg(frameNumber).arg(m_detail.asset.fileName));
    } else {
        setMessage(errorMessage);
    }
}

void MaterialCenterViewModel::analyzeVisibleSupplement()
{
    if (!m_analysisService) {
        return;
    }

    QStringList videoKeys;
    for (const auto &asset : m_assets) {
        if (canAnalyzeAsset(asset)) {
            videoKeys.append(asset.videoKey);
        }
    }
    if (videoKeys.isEmpty()) {
        setMessage(QStringLiteral("当前结果没有可补充解析的素材。"));
        return;
    }

    QString message;
    m_analysisService->enqueueVideosForSupplement(videoKeys, &message);
    setMessage(message.trimmed().isEmpty()
                   ? QStringLiteral("当前结果中没有需要补充解析的素材。")
                   : message);
}

void MaterialCenterViewModel::analyzeVisibleAll()
{
    if (!m_analysisService) {
        return;
    }

    QStringList videoKeys;
    for (const auto &asset : m_assets) {
        if (canAnalyzeAsset(asset)) {
            videoKeys.append(asset.videoKey);
        }
    }
    if (videoKeys.isEmpty()) {
        setMessage(QStringLiteral("当前结果没有可解析的素材。"));
        return;
    }

    QString message;
    m_analysisService->enqueueVideosForRebuild(videoKeys, &message);
    setMessage(message.trimmed().isEmpty()
                   ? QStringLiteral("当前结果没有可重新解析的素材。")
                   : message);
}

bool MaterialCenterViewModel::openSelectedProject()
{
    if (!hasSelection() || !m_projectService) {
        return false;
    }

    if (m_detail.asset.projectDatabasePath.trimmed().isEmpty() && m_queryService) {
        loadDetailPage(m_detail.asset.videoKey, 0, true);
        setMessage(QStringLiteral("正在加载素材所属项目信息，请稍后重试。"));
        return false;
    }

    if (m_detail.asset.projectDatabasePath.trimmed().isEmpty()) {
        setMessage(QStringLiteral("无法找到该素材所属项目，不能打开详情。"));
        return false;
    }

    if (m_projectService->hasOpenProject()
        && sameProjectDatabasePath(m_projectService->currentProject().databasePath,
                                   m_detail.asset.projectDatabasePath)) {
        setMessage(QStringLiteral("素材所属工程已打开。"));
        return true;
    }

    QString errorMessage;
    if (!m_projectService->openProject(m_detail.asset.projectDatabasePath, &errorMessage)) {
        setMessage(errorMessage);
        return false;
    } else {
        setMessage(QStringLiteral("已打开项目：%1").arg(m_detail.asset.projectName));
    }
    return true;
}

bool MaterialCenterViewModel::openQuickSearchResult(const QString &videoKey)
{
    return openQuickSearchResultAtIndex(videoKey, -1);
}

bool MaterialCenterViewModel::openQuickSearchResultAtIndex(const QString &videoKey,
                                                             int resultIndex)
{
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        setMessage(QStringLiteral("快捷搜索结果缺少素材标识，无法打开。"));
        return false;
    }

    const auto targetAsset = assetForFileAction(normalizedKey);
    if (targetAsset.videoKey.trimmed().isEmpty()
        || targetAsset.projectDatabasePath.trimmed().isEmpty()) {
        setMessage(QStringLiteral("无法找到快捷搜索结果所属工程，不能打开素材。"));
        return false;
    }

    int targetFrameNumber = -1;
    if (frameSearchMode()
        && resultIndex >= 0
        && resultIndex < m_frames.size()
        && m_frames.at(resultIndex).videoKey == normalizedKey) {
        targetFrameNumber = m_frames.at(resultIndex).frameNumber;
    }

    const auto quickSearchQuery = m_searchText;
    const auto targetProjectUuid = targetAsset.projectUuid.trimmed();
    selectVideo(normalizedKey);
    if (!openSelectedProject()) {
        return false;
    }

    // Opening another project clears ShellViewModel's global search text. Keep
    // the quick-search query stable so the target remains in the result set.
    if (m_searchText != quickSearchQuery) {
        setSearchText(quickSearchQuery);
        m_searchRefreshTimer->stop();
    }

    m_quickSearchRevealVideoKey = normalizedKey;
    m_quickSearchRevealFrameNumber = targetFrameNumber;
    if (!targetProjectUuid.isEmpty()) {
        m_projectFilter = targetProjectUuid;
        m_sourceFilter.clear();
        m_analysisStatusFilter = -1;
        reload();
        selectVideo(normalizedKey);
    }
    ++m_quickSearchRevealRevision;
    emit quickSearchRevealChanged();
    emit quickSearchNavigationRequested(quickSearchQuery);
    setMessage(QStringLiteral("已进入素材所属工程，并在素材管理中心定位目标。"));
    return true;
}

bool MaterialCenterViewModel::openQuickSearchFolderResult(const QString &folderKey)
{
    if (!m_projectService) {
        return false;
    }

    const auto folder = folderByKey(folderKey);
    if (folder.folderKey.trimmed().isEmpty()
        || folder.projectDatabasePath.trimmed().isEmpty()) {
        setMessage(QStringLiteral("无法找到该文件夹所属工程。"));
        return false;
    }

    const auto quickSearchQuery = m_searchText;
    if (!m_projectService->hasOpenProject()
        || !sameProjectDatabasePath(m_projectService->currentProject().databasePath,
                                    folder.projectDatabasePath)) {
        QString errorMessage;
        if (!m_projectService->openProject(folder.projectDatabasePath, &errorMessage)) {
            setMessage(errorMessage);
            return false;
        }
    }

    if (m_searchText != quickSearchQuery) {
        setSearchText(quickSearchQuery);
        m_searchRefreshTimer->stop();
    }
    m_projectFilter = folder.projectUuid.trimmed();
    m_sourceFilter.clear();
    m_analysisStatusFilter = -1;
    reload();
    emit quickSearchNavigationRequested(quickSearchQuery);
    setMessage(QStringLiteral("已进入文件夹所属工程的素材管理中心。"));
    return true;
}

void MaterialCenterViewModel::locateSelectedSource()
{
    if (!hasSelection()) {
        return;
    }

    QString errorMessage;
    if (!FileRevealService::revealFile(m_detail.asset.absolutePath, &errorMessage)) {
        setMessage(errorMessage.isEmpty() ? QStringLiteral("无法定位素材文件。") : errorMessage);
        return;
    }
    setMessage(QStringLiteral("已打开所在目录并选中素材。"));
}

QString MaterialCenterViewModel::quickSearchFolderKeyAt(int resultIndex) const
{
    return resultIndex >= 0 && resultIndex < m_folders.size()
        ? m_folders.at(resultIndex).folderKey
        : QString();
}

QString MaterialCenterViewModel::quickSearchVideoKeyAt(int resultIndex) const
{
    if (resultIndex < 0) {
        return {};
    }
    if (frameSearchMode()) {
        return resultIndex < m_frames.size()
            ? m_frames.at(resultIndex).videoKey
            : QString();
    }
    return resultIndex < m_assets.size()
        ? m_assets.at(resultIndex).videoKey
        : QString();
}

bool MaterialCenterViewModel::openAssetFolder(const QString &videoKey)
{
    const auto asset = assetForFileAction(videoKey);
    if (asset.videoKey.trimmed().isEmpty() || asset.absolutePath.trimmed().isEmpty()) {
        setMessage(QStringLiteral("素材所在目录当前不可用。"));
        return false;
    }

    QString errorMessage;
    if (!FileRevealService::revealFile(asset.absolutePath, &errorMessage)) {
        setMessage(errorMessage.isEmpty() ? QStringLiteral("无法打开素材所在目录。") : errorMessage);
        return false;
    }
    setMessage(QStringLiteral("已打开所在目录并选中素材。"));
    return true;
}

bool MaterialCenterViewModel::copyAssetPath(const QString &videoKey)
{
    const auto asset = assetForFileAction(videoKey);
    auto *clipboard = QGuiApplication::clipboard();
    if (asset.videoKey.trimmed().isEmpty() || asset.absolutePath.trimmed().isEmpty() || !clipboard) {
        setMessage(QStringLiteral("素材文件路径不可用，无法复制。"));
        return false;
    }

    clipboard->setText(QDir::toNativeSeparators(QFileInfo(asset.absolutePath).absoluteFilePath()));
    setMessage(QStringLiteral("已复制素材文件路径。"));
    return true;
}

void MaterialCenterViewModel::openFolderProject(const QString &folderKey)
{
    if (!m_projectService) {
        return;
    }

    const auto folder = folderByKey(folderKey);
    if (folder.folderKey.isEmpty()) {
        setMessage(QStringLiteral("无法找到该文件夹所属项目。"));
        return;
    }

    QString errorMessage;
    if (!m_projectService->openProject(folder.projectDatabasePath, &errorMessage)) {
        setMessage(errorMessage);
    } else {
        setMessage(QStringLiteral("已打开项目：%1").arg(folder.projectName));
    }
}

void MaterialCenterViewModel::locateFolder(const QString &folderKey)
{
    const auto folder = folderByKey(folderKey);
    const QFileInfo info(folder.absolutePath);
    if (folder.folderKey.isEmpty() || !folder.available || !info.exists() || !info.isDir()) {
        setMessage(QStringLiteral("文件夹当前不可用，无法定位。"));
        return;
    }

    QString errorMessage;
    if (!FileRevealService::openDirectory(info.absoluteFilePath(), &errorMessage)) {
        setMessage(errorMessage.isEmpty()
                       ? QStringLiteral("无法打开文件夹：%1").arg(info.absoluteFilePath())
                       : errorMessage);
    }
}

bool MaterialCenterViewModel::copyFolderPath(const QString &folderKey)
{
    const auto folder = folderByKey(folderKey);
    auto *clipboard = QGuiApplication::clipboard();
    if (folder.folderKey.trimmed().isEmpty() || folder.absolutePath.trimmed().isEmpty() || !clipboard) {
        setMessage(QStringLiteral("文件夹路径不可用，无法复制。"));
        return false;
    }

    clipboard->setText(QDir::toNativeSeparators(QFileInfo(folder.absolutePath).absoluteFilePath()));
    setMessage(QStringLiteral("已复制文件夹路径。"));
    return true;
}

void MaterialCenterViewModel::toggleSelectedFramesExpanded()
{
    if (!canExpandSelectedFrames()) {
        return;
    }
    if (selectedFramesExpanded()) {
        collapseSelectedFrames();
    } else {
        showAllSelectedFrames();
    }
}

void MaterialCenterViewModel::loadMoreSelectedFrames()
{
    if (!canLoadMoreSelectedFrames()) {
        return;
    }
    loadDetailPage(m_detail.asset.videoKey, m_selectedFrameCursor, false);
}

void MaterialCenterViewModel::showAllSelectedFrames()
{
    loadMoreSelectedFrames();
}

void MaterialCenterViewModel::collapseSelectedFrames()
{
    // 帧详情由数据库分页和虚拟化视图控制，不再回收已加载页。
}

GlobalVideoAsset MaterialCenterViewModel::assetByVideoKey(const QString &videoKey) const
{
    const auto normalizedKey = videoKey.trimmed();
    for (const auto &asset : m_assets) {
        if (asset.videoKey == normalizedKey) {
            return asset;
        }
    }
    return {};
}

GlobalVideoAsset MaterialCenterViewModel::assetForFileAction(const QString &videoKey) const
{
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        return {};
    }

    auto asset = assetByVideoKey(normalizedKey);
    if (!asset.videoKey.trimmed().isEmpty()) {
        return asset;
    }
    if (m_detail.asset.videoKey == normalizedKey
        && !m_detail.asset.absolutePath.trimmed().isEmpty()) {
        return m_detail.asset;
    }
    return {};
}

FolderSearchHit MaterialCenterViewModel::folderByKey(const QString &folderKey) const
{
    const auto normalizedKey = folderKey.trimmed();
    for (const auto &folder : m_folders) {
        if (folder.folderKey == normalizedKey) {
            return folder;
        }
    }
    return {};
}

void MaterialCenterViewModel::prepareSelection(const QString &videoKey)
{
    const auto normalizedKey = videoKey.trimmed();

    ++m_detailRequestGeneration;
    m_detailRefreshTimer->stop();
    m_contactSheetBuildTimer->stop();
    m_pendingDetailVideoKey.clear();
    m_pendingContactSheetVideoKey.clear();
    m_selectedAllFramesCache.clear();
    m_selectedFramesCache.clear();
    m_selectedFrameSearchStatusCache.clear();
    m_selectedThumbnailUrlCache = {};
    m_selectedTotalFrameCount = 0;
    m_selectedFrameCursor = 0;
    m_selectedFramesHasMore = false;
    m_selectedFramesLoading = false;
    m_detail = {};

    if (normalizedKey.isEmpty()) {
        return;
    }

    const auto asset = assetByVideoKey(normalizedKey);
    if (!asset.videoKey.trimmed().isEmpty()) {
        m_detail.asset = asset;
    } else {
        m_detail.asset.videoKey = normalizedKey;
    }

    refreshSelectedThumbnailUrl(false);
    m_selectedFramesLoading = true;
    m_selectedFrameSearchStatusCache = QStringLiteral("正在加载逐帧解析...");
    m_pendingDetailVideoKey = normalizedKey;
    m_detailRefreshTimer->start();
}

void MaterialCenterViewModel::loadPendingDetail()
{
    const auto videoKey = m_pendingDetailVideoKey.trimmed();
    m_pendingDetailVideoKey.clear();

    if (!m_queryService || videoKey.isEmpty()) {
        return;
    }

    loadDetailPage(videoKey, 0, true);
}

void MaterialCenterViewModel::loadDetailPage(const QString &videoKey,
                                             int afterFrameNumber,
                                             bool replaceCurrent)
{
    if (!m_queryService || videoKey.trimmed().isEmpty()
        || (m_selectedFramesLoading && !replaceCurrent)) {
        return;
    }

    const auto normalizedKey = videoKey.trimmed();
    const auto requestGeneration = ++m_detailRequestGeneration;
    const auto backendGeneration = m_queryBackendGeneration;
    const auto databasePath = m_queryService->databaseFilePath();
    const auto preferredFrameNumber = replaceCurrent && m_detail.asset.videoKey == normalizedKey
        ? m_detail.asset.matchedFrameNumber
        : -1;
    m_selectedFramesLoading = true;
    if (replaceCurrent) {
        m_selectedFrameSearchStatusCache = QStringLiteral("正在加载逐帧解析...");
    }
    emit selectionChanged();

    auto *watcher = new QFutureWatcher<MaterialCenterDetailTaskResult>(this);
    connect(watcher, &QFutureWatcher<MaterialCenterDetailTaskResult>::finished, this,
            [this, watcher]() {
        auto task = watcher->result();
        watcher->deleteLater();
        if (task.requestGeneration != m_detailRequestGeneration
            || task.videoKey != m_detail.asset.videoKey) {
            return;
        }
        m_selectedFramesLoading = false;
        if (!task.errorMessage.isEmpty()
            || task.page.detail.asset.videoKey.trimmed().isEmpty()) {
            m_selectedFrameSearchStatusCache = task.errorMessage.isEmpty()
                ? QStringLiteral("无法读取当前素材详情")
                : task.errorMessage;
            emit selectionChanged();
            return;
        }

        const auto selectedAsset = assetByVideoKey(task.videoKey);
        if (task.replaceCurrent) {
            m_detail = std::move(task.page.detail);
            if (!selectedAsset.videoKey.trimmed().isEmpty()) {
                m_detail.asset = selectedAsset;
            }
        } else {
            QSet<int> existingFrameNumbers;
            for (const auto &frame : std::as_const(m_detail.frames)) {
                existingFrameNumbers.insert(frame.frameNumber);
            }
            for (auto &frame : task.page.detail.frames) {
                if (!existingFrameNumbers.contains(frame.frameNumber)) {
                    existingFrameNumbers.insert(frame.frameNumber);
                    m_detail.frames.append(std::move(frame));
                }
            }
            std::sort(m_detail.frames.begin(), m_detail.frames.end(), [](const auto &left, const auto &right) {
                return left.frameNumber < right.frameNumber;
            });
        }
        m_selectedTotalFrameCount = task.page.totalFrameCount;
        m_selectedFrameCursor = task.page.nextFrameNumber;
        m_selectedFramesHasMore = task.page.hasMoreFrames;
        refreshSelectedCaches();
        emit selectionChanged();
        emit analysisProgressChanged();
        emit dimensionAnalysisChanged();
    });
    watcher->setFuture(QtConcurrent::run(
        &m_detailPool,
        [databasePath,
         backendGeneration,
         requestGeneration,
         normalizedKey,
         afterFrameNumber,
         preferredFrameNumber,
         replaceCurrent]() {
            MaterialCenterDetailTaskResult task;
            task.requestGeneration = requestGeneration;
            task.videoKey = normalizedKey;
            task.replaceCurrent = replaceCurrent;
            auto *context = materialCenterReadContext(
                databasePath, backendGeneration, &task.errorMessage);
            if (!context || !context->queryService) {
                return task;
            }
            task.page = context->queryService->fetchDetailPage(
                normalizedKey,
                kVisibleFrameBatchSize,
                afterFrameNumber,
                preferredFrameNumber);
            return task;
        }));
}

void MaterialCenterViewModel::refreshSelectedCaches()
{
    m_selectedAllFramesCache.clear();
    m_selectedFramesCache.clear();

    if (!hasSelection()) {
        m_selectedTotalFrameCount = 0;
        m_selectedFrameCursor = 0;
        m_selectedFramesHasMore = false;
        m_selectedFramesLoading = false;
        m_selectedFrameSearchStatusCache.clear();
        m_selectedThumbnailUrlCache = {};
        return;
    }

    m_selectedFramesLoading = false;
    auto terms = m_lastParsedQuery.lexicalTerms;
    if (terms.isEmpty() && !m_lastParsedQuery.semanticText.trimmed().isEmpty()) {
        terms = searchTerms(m_lastParsedQuery.semanticText);
    }
    int matchCount = 0;

    for (const auto &frame : m_detail.frames) {
        const bool semanticFrameMatch = m_detail.asset.matchedFrameNumber >= 0
            && frame.frameNumber == m_detail.asset.matchedFrameNumber;
        if (!semanticFrameMatch && !frameMatches(frame, terms)) {
            continue;
        }
        ++matchCount;
        auto matches = matchedFrameTerms(frame, terms);
        if (semanticFrameMatch) {
            matches.prepend(QStringLiteral("视觉语义命中"));
        }
        m_selectedAllFramesCache.append(QVariantMap{
            {QStringLiteral("frameNumber"), frame.frameNumber},
            {QStringLiteral("timestampLabel"), Formatters::formatFrameTimestamp(frame.timestampMs)},
            {QStringLiteral("imagePath"), frame.imagePath},
            {QStringLiteral("caption"), frame.caption},
            {QStringLiteral("tags"), frame.tags.join(QStringLiteral("、"))},
            {QStringLiteral("objects"), frame.objects.join(QStringLiteral("、"))},
            {QStringLiteral("actions"), frame.actions},
            {QStringLiteral("setting"), frame.setting},
            {QStringLiteral("errorMessage"), frame.errorMessage},
            {QStringLiteral("matchText"), matches.join(QStringLiteral("、"))},
            {QStringLiteral("analysisState"), static_cast<int>(frame.analysisState)},
            {QStringLiteral("retryCount"), frame.retryCount},
            {QStringLiteral("retryLabel"), retryLabel(frame)},
            {QStringLiteral("lastAttemptAt"), frame.lastAttemptAt},
            {QStringLiteral("canRetry"), canRetryFrame(frame)}
        });
    }

    applySelectedFrameExpansion();

    if (terms.isEmpty()) {
        m_selectedFrameSearchStatusCache = m_selectedFramesHasMore
            ? QStringLiteral("共 %1 帧解析结果，已加载 %2 帧")
                  .arg(m_selectedTotalFrameCount)
                  .arg(m_detail.frames.size())
            : QStringLiteral("共 %1 帧解析结果").arg(m_selectedTotalFrameCount);
    } else if (matchCount == 0) {
        m_selectedFrameSearchStatusCache = QStringLiteral("搜索“%1”未命中该素材的逐帧解析").arg(m_searchText.trimmed());
    } else {
        m_selectedFrameSearchStatusCache = QStringLiteral("搜索“%1”命中 %2/%3 帧")
            .arg(m_searchText.trimmed())
            .arg(matchCount)
            .arg(m_selectedTotalFrameCount);
    }

    refreshSelectedThumbnailUrl(true);
}

void MaterialCenterViewModel::applySelectedFrameExpansion()
{
    m_selectedFramesCache = m_selectedAllFramesCache;
}

void MaterialCenterViewModel::refreshSelectedThumbnailUrl(bool allowContactSheetBuild)
{
    m_selectedThumbnailUrlCache = {};

    if (!hasSelection()) {
        return;
    }

    if (m_detail.asset.assetType == AssetType::Image
        && QFile::exists(m_detail.asset.absolutePath)) {
        m_selectedThumbnailUrlCache = QUrl::fromLocalFile(m_detail.asset.absolutePath);
        return;
    }

    const int frameCount = m_settings ? m_settings->contactSheetFrameCount() : 24;
    const auto frameImagePaths = contactSheetImagePaths(m_detail, frameCount);

    if (!frameImagePaths.isEmpty()) {
        const auto contactSheetPath = Paths::projectContactSheetPath(m_detail.asset.projectDatabasePath,
                                                                     m_detail.asset.videoKey,
                                                                     frameCount);
        if (QFile::exists(contactSheetPath)) {
            m_selectedThumbnailUrlCache = QUrl::fromLocalFile(contactSheetPath);
            return;
        }

        if (allowContactSheetBuild) {
            m_pendingContactSheetVideoKey = m_detail.asset.videoKey;
            m_contactSheetBuildTimer->start();
        }
    }

    if (!m_detail.asset.thumbnailPath.trimmed().isEmpty()) {
        m_selectedThumbnailUrlCache = QUrl::fromLocalFile(m_detail.asset.thumbnailPath);
    }
}

void MaterialCenterViewModel::buildPendingContactSheet()
{
    const auto videoKey = m_pendingContactSheetVideoKey.trimmed();
    m_pendingContactSheetVideoKey.clear();

    if (videoKey.isEmpty()) {
        return;
    }

    VideoAnalysisDetail detail;
    if (m_detail.asset.videoKey == videoKey) {
        detail = m_detail;
    } else {
        return;
    }

    const int frameCount = m_settings ? m_settings->contactSheetFrameCount() : 24;
    const auto frameImagePaths = contactSheetImagePaths(detail, frameCount);
    if (frameImagePaths.isEmpty()) {
        return;
    }

    const auto contactSheetPath = Paths::projectContactSheetPath(detail.asset.projectDatabasePath,
                                                                 detail.asset.videoKey,
                                                                 frameCount);
    if (QFile::exists(contactSheetPath)) {
        m_selectedThumbnailUrlCache = QUrl::fromLocalFile(contactSheetPath);
        emit selectionChanged();
        return;
    }

    const auto requestGeneration = m_detailRequestGeneration;
    auto *watcher = new QFutureWatcher<ContactSheetTaskResult>(this);
    connect(watcher, &QFutureWatcher<ContactSheetTaskResult>::finished, this,
            [this, watcher]() {
        const auto task = watcher->result();
        watcher->deleteLater();
        if (task.requestGeneration != m_detailRequestGeneration
            || task.videoKey != m_detail.asset.videoKey
            || !task.success
            || !QFile::exists(task.outputPath)) {
            return;
        }
        m_selectedThumbnailUrlCache = QUrl::fromLocalFile(task.outputPath);
        emit selectionChanged();
    });
    watcher->setFuture(QtConcurrent::run(
        &m_detailPool,
        [requestGeneration, videoKey, frameImagePaths, frameCount, contactSheetPath]() {
            ContactSheetTaskResult task;
            task.requestGeneration = requestGeneration;
            task.videoKey = videoKey;
            task.outputPath = contactSheetPath;
            QString errorMessage;
            task.success = ContactSheetBuilder::build(
                frameImagePaths, frameCount, contactSheetPath, &errorMessage);
            return task;
        }));
}

void MaterialCenterViewModel::refreshDetail()
{
    if (!m_queryService || m_detail.asset.videoKey.trimmed().isEmpty()) {
        return;
    }
    prepareSelection(m_detail.asset.videoKey);
}

void MaterialCenterViewModel::setMessage(const QString &message)
{
    if (m_message == message) {
        return;
    }
    m_message = message;
    emit statusChanged();
}

MaterialCenterViewModel::AnalysisProgressState MaterialCenterViewModel::selectedProgressState() const
{
    const auto key = m_detail.asset.videoKey;
    if (!key.trimmed().isEmpty() && m_analysisProgressByVideoKey.contains(key)) {
        return m_analysisProgressByVideoKey.value(key);
    }
    return {};
}

MaterialCenterViewModel::DimensionProgressState MaterialCenterViewModel::selectedDimensionProgressState() const
{
    const auto key = m_detail.asset.videoKey;
    if (!key.trimmed().isEmpty() && m_dimensionProgressByVideoKey.contains(key)) {
        return m_dimensionProgressByVideoKey.value(key);
    }
    return {};
}
