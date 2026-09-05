#include "application/VideoAnalysisService.h"

#include "application/DocumentPreviewService.h"
#include "core/thumbnail/ContactSheetBuilder.h"
#include "core/jobs/JobEngine.h"
#include "infrastructure/config/AppSettings.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include "infrastructure/network/VisionApiClient.h"
#include "shared/Formatters.h"
#include "shared/Paths.h"
#include "shared/SearchConfiguration.h"
#include "shared/VisualAnalysisMetadata.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaObject>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

namespace {
constexpr int kMaxFrameRetryCount = 3;
constexpr int kAnalysisFrameMaxWidth = 1920;
constexpr int kAnalysisFrameMaxHeight = 1080;

QString structuredPromptVersion()
{
    const auto value = cinevault::searchconfig::kStructuredVisionPromptVersion;
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}

QString nonNullText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

struct AnalysisConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
    VideoFrameExtractionStrategy extractionStrategy = VideoFrameExtractionStrategy::SceneAndInterval;
    double frameIntervalSeconds = 1.0;
    double sceneThreshold = 0.3;
    double minimumSharpness = 0.01;
    int contactSheetFrameCount = 24;
    int timeoutSec = 60;
};

QString samplingPolicy(const AnalysisConfig &config)
{
    return VisualAnalysisMetadata::samplingPolicy(config.extractionStrategy,
                                                   config.frameIntervalSeconds,
                                                   config.sceneThreshold,
                                                   config.minimumSharpness,
                                                   kAnalysisFrameMaxWidth,
                                                   kAnalysisFrameMaxHeight);
}

struct FrameRequestResult {
    int frameIndex = -1;
    std::optional<VisionFrameAnalysis> analysis;
    QString errorMessage;
    int httpStatusCode = 0;
    int attempt = 0;
    bool wasAttempted = false;
};

struct DimensionFrameAnalysisRecord {
    QString dimensionKey;
    QString dimensionName;
    int frameNumber = 0;
    qint64 timestampMs = 0;
    QString imagePath;
    QString detail;
};

bool insertFrameRow(QSqlDatabase &db,
                    const QString &videoKey,
                    const ExtractedFrame &frame,
                    QString *errorMessage);

JobSubject analysisSubjectForKey(const QString &videoKey, const QString &label)
{
    JobSubject subject;
    subject.kind = QStringLiteral("asset");
    subject.key = videoKey;
    subject.name = label.trimmed().isEmpty() ? videoKey : label.trimmed();
    subject.typeLabel = QStringLiteral("素材");
    return subject;
}

JobSubject analysisSubjectForAsset(const GlobalVideoAsset &asset)
{
    JobSubject subject;
    subject.kind = QStringLiteral("asset");
    subject.key = asset.videoKey;
    subject.name = asset.fileName;
    subject.path = asset.absolutePath;
    subject.thumbnailPath = asset.assetType == AssetType::Image ? asset.absolutePath : asset.thumbnailPath;
    subject.thumbnailStatus = asset.assetType == AssetType::Image ? ThumbnailStatus::Success : asset.thumbnailStatus;
    subject.typeLabel = Formatters::assetTypeLabel(asset.assetType);
    return subject;
}

JobProgressContext analysisProgressContext(int currentStep,
                                           int totalSteps,
                                           const QString &stepLabel,
                                           qint64 currentItem = 0,
                                           qint64 totalItems = 0,
                                           const QString &unitLabel = QString(),
                                           int currentFrameNumber = 0,
                                           const QString &extraLabel = QString())
{
    JobProgressContext context;
    context.currentStep = currentStep;
    context.totalSteps = totalSteps;
    context.stepLabel = stepLabel;
    context.currentItem = currentItem;
    context.totalItems = totalItems;
    context.unitLabel = unitLabel;
    context.currentFrameNumber = currentFrameNumber;
    context.extraLabel = extraLabel;
    return context;
}

bool execQuery(QSqlQuery &query, QString *errorMessage)
{
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

QString toJson(const QStringList &items)
{
    QJsonArray array;
    for (const auto &item : items) {
        array.append(item);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList parseJsonList(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return {};
    }

    const auto document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return {};
    }

    QStringList items;
    for (const auto &value : document.array()) {
        const auto item = value.toString().trimmed();
        if (!item.isEmpty()) {
            items.append(item);
        }
    }
    return items;
}

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

bool isSupportedTextAsset(AssetType assetType, const QString &extension)
{
    static const QSet<QString> textExtensions = {
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("md"),
        QStringLiteral("json"), QStringLiteral("csv"), QStringLiteral("tsv"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("pdf"),
        QStringLiteral("docx"), QStringLiteral("xlsx"), QStringLiteral("pptx"),
        QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("vtt")
    };
    const auto normalizedExtension = extension.trimmed().toLower();
    return assetType == AssetType::Subtitle
        || (assetType == AssetType::Document && textExtensions.contains(normalizedExtension));
}

bool canAnalyzeAsset(AssetType assetType, const QString &extension)
{
    return assetType == AssetType::Video
        || assetType == AssetType::Image
        || isSupportedTextAsset(assetType, extension);
}

bool isPhotoshopAsset(const GlobalVideoAsset &asset)
{
    if (asset.assetType != AssetType::Image) {
        return false;
    }
    const auto extension = asset.extension.trimmed().toLower();
    return extension == QStringLiteral("psd") || extension == QStringLiteral("psb");
}

bool loadVideoAsset(QSqlDatabase &db, const QString &videoKey, GlobalVideoAsset *asset, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, asset_id, "
        "file_name, COALESCE(extension, ''), absolute_path, relative_path, COALESCE(asset_type, 1), size_bytes, modified_at, "
        "duration_ms, COALESCE(thumbnail_path, ''), COALESCE(thumbnail_status, 0), analysis_status, confirmation_status, "
        "COALESCE(technical_summary, ''), COALESCE(source_text, ''), COALESCE(error_message, ''), updated_at "
        "FROM global_video_asset WHERE video_key = ?"));
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage) || !query.next()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("素材管理中心找不到该素材");
        }
        return false;
    }

    asset->videoKey = query.value(0).toString();
    asset->assetKey = asset->videoKey;
    asset->projectUuid = query.value(1).toString();
    asset->projectName = query.value(2).toString();
    asset->projectDatabasePath = query.value(3).toString();
    asset->sourceRootId = query.value(4).toLongLong();
    asset->sourceRootName = query.value(5).toString();
    asset->assetId = query.value(6).toLongLong();
    asset->fileName = query.value(7).toString();
    asset->extension = query.value(8).toString();
    asset->absolutePath = query.value(9).toString();
    asset->relativePath = query.value(10).toString();
    asset->assetType = static_cast<AssetType>(query.value(11).toInt());
    asset->sizeBytes = query.value(12).toLongLong();
    asset->modifiedAt = query.value(13).toString();
    asset->durationMs = query.value(14).toLongLong();
    asset->thumbnailPath = query.value(15).toString();
    asset->thumbnailStatus = static_cast<ThumbnailStatus>(query.value(16).toInt());
    asset->analysisStatus = static_cast<VideoAnalysisStatus>(query.value(17).toInt());
    asset->confirmationStatus = static_cast<ConfirmationStatus>(query.value(18).toInt());
    asset->technicalSummary = query.value(19).toString();
    asset->sourceText = query.value(20).toString();
    asset->errorMessage = query.value(21).toString();
    asset->updatedAt = query.value(22).toString();
    return true;
}

bool loadAnalysisTask(QSqlDatabase &db, const QString &videoKey, VideoAnalysisTask *task, QString *errorMessage)
{
    if (!task) {
        return false;
    }

    *task = {};
    task->videoKey = videoKey;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT stage, total_frames, completed_frames, successful_frames, skipped_frames, summary_retry_count, "
        "COALESCE(last_error_message, ''), COALESCE(last_updated_at, '') "
        "FROM video_analysis_task WHERE video_key = ?"));
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    if (!query.next()) {
        return true;
    }

    task->stage = static_cast<VideoAnalysisTaskStage>(query.value(0).toInt());
    task->totalFrames = query.value(1).toInt();
    task->completedFrames = query.value(2).toInt();
    task->successfulFrames = query.value(3).toInt();
    task->skippedFrames = query.value(4).toInt();
    task->summaryRetryCount = query.value(5).toInt();
    task->lastErrorMessage = query.value(6).toString();
    task->lastUpdatedAt = query.value(7).toString();
    return true;
}

bool persistAnalysisTask(QSqlDatabase &db, const VideoAnalysisTask &task, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO video_analysis_task "
        "(video_key, stage, total_frames, completed_frames, successful_frames, skipped_frames, summary_retry_count, "
        "last_error_message, last_updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key) DO UPDATE SET "
        "stage = excluded.stage, "
        "total_frames = excluded.total_frames, "
        "completed_frames = excluded.completed_frames, "
        "successful_frames = excluded.successful_frames, "
        "skipped_frames = excluded.skipped_frames, "
        "summary_retry_count = excluded.summary_retry_count, "
        "last_error_message = excluded.last_error_message, "
        "last_updated_at = excluded.last_updated_at"));
    const auto normalizedLastErrorMessage = task.lastErrorMessage.isNull()
        ? QStringLiteral("")
        : task.lastErrorMessage;
    const auto lastUpdatedAt = task.lastUpdatedAt.isEmpty() ? nowIso() : task.lastUpdatedAt;
    query.addBindValue(task.videoKey);
    query.addBindValue(static_cast<int>(task.stage));
    query.addBindValue(task.totalFrames);
    query.addBindValue(task.completedFrames);
    query.addBindValue(task.successfulFrames);
    query.addBindValue(task.skippedFrames);
    query.addBindValue(task.summaryRetryCount);
    query.addBindValue(normalizedLastErrorMessage);
    query.addBindValue(lastUpdatedAt);
    return execQuery(query, errorMessage);
}

bool deleteAnalysisTask(QSqlDatabase &db, const QString &videoKey, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM video_analysis_task WHERE video_key = ?"));
    query.addBindValue(videoKey);
    return execQuery(query, errorMessage);
}

QVector<FrameAnalysisRecord> loadFrameRows(QSqlDatabase &db, const QString &videoKey, QString *errorMessage)
{
    QVector<FrameAnalysisRecord> frames;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, frame_number, timestamp_ms, COALESCE(image_path, ''), COALESCE(caption, ''), "
        "COALESCE(tags_json, '[]'), COALESCE(objects_json, '[]'), COALESCE(actions, ''), COALESCE(setting_text, ''), "
        "COALESCE(entities_json, '[]'), COALESCE(ocr_text, ''), COALESCE(ocr_blocks_json, '[]'), "
        "COALESCE(structured_profile_version, 1), COALESCE(facts_complete, 0), COALESCE(model_name, ''), "
        "COALESCE(prompt_version, ''), COALESCE(analyzed_at, ''), COALESCE(error_message, ''), "
        "COALESCE(analysis_state, 0), COALESCE(retry_count, 0), COALESCE(last_http_status, 0), COALESCE(last_attempt_at, '') "
        "FROM video_frame_analysis WHERE video_key = ? ORDER BY frame_number"));
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage)) {
        return {};
    }

    while (query.next()) {
        FrameAnalysisRecord frame;
        frame.id = query.value(0).toLongLong();
        frame.videoKey = videoKey;
        frame.frameNumber = query.value(1).toInt();
        frame.timestampMs = query.value(2).toLongLong();
        frame.imagePath = query.value(3).toString();
        frame.caption = query.value(4).toString();
        frame.tags = parseJsonList(query.value(5).toString());
        frame.objects = parseJsonList(query.value(6).toString());
        frame.actions = query.value(7).toString();
        frame.setting = query.value(8).toString();
        frame.entities = VisualAnalysisMetadata::entityFactsFromJson(query.value(9).toString());
        frame.ocrText = query.value(10).toString();
        frame.ocrBlocks = parseJsonList(query.value(11).toString());
        frame.structuredProfileVersion = query.value(12).toInt();
        frame.factsComplete = query.value(13).toBool();
        frame.modelName = query.value(14).toString();
        frame.promptVersion = query.value(15).toString();
        frame.analyzedAt = query.value(16).toString();
        frame.errorMessage = query.value(17).toString();
        frame.analysisState = static_cast<FrameAnalysisState>(query.value(18).toInt());
        frame.retryCount = query.value(19).toInt();
        frame.lastHttpStatus = query.value(20).toInt();
        frame.lastAttemptAt = query.value(21).toString();
        frames.append(frame);
    }
    return frames;
}

QVector<FrameAnalysisRecord> successfulFrames(const QVector<FrameAnalysisRecord> &frames)
{
    QVector<FrameAnalysisRecord> items;
    for (const auto &frame : frames) {
        const auto hasAnalysisContent = !frame.caption.trimmed().isEmpty()
            || !frame.tags.isEmpty()
            || !frame.objects.isEmpty()
            || !frame.actions.trimmed().isEmpty()
            || !frame.setting.trimmed().isEmpty();
        const auto isLegacySuccessfulFrame = frame.analysisState == FrameAnalysisState::Pending
            && hasAnalysisContent
            && frame.errorMessage.trimmed().isEmpty();
        if ((frame.analysisState == FrameAnalysisState::Success || isLegacySuccessfulFrame)
            && frame.errorMessage.trimmed().isEmpty()) {
            items.append(frame);
        }
    }
    return items;
}

QString dimensionKey(QString name)
{
    return name.simplified().toCaseFolded();
}

QStringList normalizedDimensionNames(const QStringList &dimensions)
{
    QStringList normalized;
    for (const auto &dimension : dimensions) {
        const auto name = dimension.simplified();
        const auto key = dimensionKey(name);
        if (key.isEmpty()) {
            continue;
        }
        bool exists = false;
        for (const auto &existing : normalized) {
            if (dimensionKey(existing) == key) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            normalized.append(name);
        }
    }
    return normalized;
}

QVector<FrameAnalysisRecord> sampleFramesForContext(const QVector<FrameAnalysisRecord> &frames, int maxFrames)
{
    if (maxFrames <= 0 || frames.size() <= maxFrames) {
        return frames;
    }
    if (maxFrames == 1) {
        return QVector<FrameAnalysisRecord>{frames.first()};
    }

    QVector<FrameAnalysisRecord> sampled;
    sampled.reserve(maxFrames);
    QSet<int> usedIndexes;
    for (int index = 0; index < maxFrames; ++index) {
        const auto mappedIndex = static_cast<int>((static_cast<qint64>(index) * (frames.size() - 1)) / (maxFrames - 1));
        if (usedIndexes.contains(mappedIndex)) {
            continue;
        }
        usedIndexes.insert(mappedIndex);
        sampled.append(frames.at(mappedIndex));
    }
    return sampled;
}

bool loadAnalysisSummary(QSqlDatabase &db, const QString &videoKey, VisionVideoSummary *summary, QString *errorMessage)
{
    if (!summary) {
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(summary, ''), COALESCE(keywords_json, '[]'), COALESCE(scenes_json, '[]') "
        "FROM video_analysis_result WHERE video_key = ?"));
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    if (!query.next()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前素材还没有基础解析结果，请先完成内容解析。");
        }
        return false;
    }

    summary->summary = query.value(0).toString();
    summary->keywords = parseJsonList(query.value(1).toString());
    summary->scenes = parseJsonList(query.value(2).toString());
    return true;
}

QVector<MaterialDimensionAnalysis> loadDimensionAnalyses(QSqlDatabase &db,
                                                         const QString &videoKey,
                                                         QString *errorMessage)
{
    QVector<MaterialDimensionAnalysis> analyses;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT dimension_name, detail, analyzed_at "
        "FROM material_dimension_analysis WHERE video_key = ? ORDER BY analyzed_at DESC, id DESC"));
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage)) {
        return analyses;
    }

    while (query.next()) {
        MaterialDimensionAnalysis analysis;
        analysis.name = query.value(0).toString();
        analysis.detail = query.value(1).toString();
        analysis.analyzedAt = query.value(2).toString();
        analyses.append(analysis);
    }
    return analyses;
}

QStringList filterNewDimensions(const QStringList &requestedDimensions,
                                const QVector<MaterialDimensionAnalysis> &existingDimensions)
{
    QStringList filtered;
    for (const auto &requested : requestedDimensions) {
        const auto requestedKey = dimensionKey(requested);
        bool exists = false;
        for (const auto &existing : existingDimensions) {
            if (dimensionKey(existing.name) == requestedKey) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            filtered.append(requested);
        }
    }
    return filtered;
}

QString buildFrameContextLine(const FrameAnalysisRecord &frame)
{
    QStringList parts;
    if (!frame.caption.trimmed().isEmpty()) {
        parts.append(QStringLiteral("描述：%1").arg(frame.caption.trimmed()));
    }
    if (!frame.tags.isEmpty()) {
        parts.append(QStringLiteral("标签：%1").arg(frame.tags.join(QStringLiteral("、"))));
    }
    if (!frame.objects.isEmpty()) {
        parts.append(QStringLiteral("对象：%1").arg(frame.objects.join(QStringLiteral("、"))));
    }
    if (!frame.actions.trimmed().isEmpty()) {
        parts.append(QStringLiteral("动作：%1").arg(frame.actions.trimmed()));
    }
    if (!frame.setting.trimmed().isEmpty()) {
        parts.append(QStringLiteral("场景：%1").arg(frame.setting.trimmed()));
    }
    const auto entityTerms = VisualAnalysisMetadata::entityFactSearchTerms(frame.entities);
    if (!entityTerms.isEmpty()) {
        parts.append(QStringLiteral("实体事实：%1").arg(entityTerms.join(QStringLiteral("、"))));
    }
    if (!frame.ocrText.trimmed().isEmpty()) {
        parts.append(QStringLiteral("画面文字：%1").arg(frame.ocrText.trimmed()));
    }
    return parts.isEmpty()
        ? QString()
        : QStringLiteral("第 %1 帧：%2").arg(frame.frameNumber).arg(parts.join(QStringLiteral("；")));
}

QString buildFrameDimensionContext(const FrameAnalysisRecord &frame)
{
    QStringList lines;
    lines.append(QStringLiteral("帧号：%1").arg(frame.frameNumber));
    lines.append(QStringLiteral("时间戳：%1 ms").arg(frame.timestampMs));
    const auto frameLine = buildFrameContextLine(frame);
    if (!frameLine.trimmed().isEmpty()) {
        lines.append(frameLine);
    }
    if (!frame.errorMessage.trimmed().isEmpty()) {
        lines.append(QStringLiteral("基础帧解析备注：%1").arg(frame.errorMessage.trimmed()));
    }
    return lines.join(QStringLiteral("\n"));
}

QHash<QString, QSet<int>> loadCompletedDimensionFrames(QSqlDatabase &db,
                                                       const QString &videoKey,
                                                       const QStringList &dimensions,
                                                       QString *errorMessage)
{
    QHash<QString, QSet<int>> completed;
    QSet<QString> requestedKeys;
    for (const auto &dimension : dimensions) {
        const auto key = dimensionKey(dimension);
        if (!key.isEmpty()) {
            requestedKeys.insert(key);
        }
    }
    if (requestedKeys.isEmpty()) {
        return completed;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT dimension_key, frame_number "
        "FROM material_dimension_frame_analysis "
        "WHERE video_key = ? AND analysis_state = ?"));
    query.addBindValue(videoKey);
    query.addBindValue(static_cast<int>(FrameAnalysisState::Success));
    if (!execQuery(query, errorMessage)) {
        return {};
    }

    while (query.next()) {
        const auto key = query.value(0).toString();
        if (!requestedKeys.contains(key)) {
            continue;
        }
        completed[key].insert(query.value(1).toInt());
    }
    return completed;
}

bool persistDimensionFrameAnalyses(QSqlDatabase &db,
                                   const QString &videoKey,
                                   const FrameAnalysisRecord &frame,
                                   const QVector<MaterialDimensionAnalysis> &analyses,
                                   const QString &modelName,
                                   QString *errorMessage)
{
    const auto analyzedAt = nowIso();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO material_dimension_frame_analysis "
        "(video_key, dimension_key, dimension_name, frame_number, timestamp_ms, image_path, detail, error_message, "
        "analysis_state, model_name, prompt_version, analyzed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, '', ?, ?, ?, ?) "
        "ON CONFLICT(video_key, dimension_key, frame_number) DO UPDATE SET "
        "dimension_name = excluded.dimension_name, "
        "timestamp_ms = excluded.timestamp_ms, "
        "image_path = excluded.image_path, "
        "detail = excluded.detail, "
        "error_message = '', "
        "analysis_state = excluded.analysis_state, "
        "model_name = excluded.model_name, "
        "prompt_version = excluded.prompt_version, "
        "analyzed_at = excluded.analyzed_at"));

    for (const auto &analysis : analyses) {
        const auto key = dimensionKey(analysis.name);
        const auto detail = analysis.detail.simplified();
        if (key.isEmpty() || detail.isEmpty()) {
            continue;
        }
        query.addBindValue(videoKey);
        query.addBindValue(key);
        query.addBindValue(analysis.name.simplified());
        query.addBindValue(frame.frameNumber);
        query.addBindValue(frame.timestampMs);
        query.addBindValue(frame.imagePath);
        query.addBindValue(detail);
        query.addBindValue(static_cast<int>(FrameAnalysisState::Success));
        query.addBindValue(modelName);
        query.addBindValue(QStringLiteral("v1-dimension-frame"));
        query.addBindValue(analyzedAt);
        if (!execQuery(query, errorMessage)) {
            return false;
        }
        query.finish();
    }
    return true;
}

bool persistDimensionFrameFailures(QSqlDatabase &db,
                                   const QString &videoKey,
                                   const FrameAnalysisRecord &frame,
                                   const QStringList &dimensions,
                                   const QString &modelName,
                                   const QString &failureMessage,
                                   QString *errorMessage)
{
    const auto analyzedAt = nowIso();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO material_dimension_frame_analysis "
        "(video_key, dimension_key, dimension_name, frame_number, timestamp_ms, image_path, detail, error_message, "
        "analysis_state, model_name, prompt_version, analyzed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, '', ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key, dimension_key, frame_number) DO UPDATE SET "
        "dimension_name = excluded.dimension_name, "
        "timestamp_ms = excluded.timestamp_ms, "
        "image_path = excluded.image_path, "
        "error_message = excluded.error_message, "
        "analysis_state = excluded.analysis_state, "
        "model_name = excluded.model_name, "
        "prompt_version = excluded.prompt_version, "
        "analyzed_at = excluded.analyzed_at"));

    for (const auto &dimension : dimensions) {
        const auto name = dimension.simplified();
        const auto key = dimensionKey(name);
        if (key.isEmpty()) {
            continue;
        }
        query.addBindValue(videoKey);
        query.addBindValue(key);
        query.addBindValue(name);
        query.addBindValue(frame.frameNumber);
        query.addBindValue(frame.timestampMs);
        query.addBindValue(frame.imagePath);
        query.addBindValue(failureMessage.simplified());
        query.addBindValue(static_cast<int>(FrameAnalysisState::Failed));
        query.addBindValue(modelName);
        query.addBindValue(QStringLiteral("v1-dimension-frame"));
        query.addBindValue(analyzedAt);
        if (!execQuery(query, errorMessage)) {
            return false;
        }
        query.finish();
    }
    return true;
}

QVector<DimensionFrameAnalysisRecord> loadDimensionFrameAnalyses(QSqlDatabase &db,
                                                                 const QString &videoKey,
                                                                 const QStringList &dimensions,
                                                                 QString *errorMessage)
{
    QVector<DimensionFrameAnalysisRecord> records;
    QSet<QString> requestedKeys;
    for (const auto &dimension : dimensions) {
        const auto key = dimensionKey(dimension);
        if (!key.isEmpty()) {
            requestedKeys.insert(key);
        }
    }
    if (requestedKeys.isEmpty()) {
        return records;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT dimension_key, dimension_name, frame_number, timestamp_ms, COALESCE(image_path, ''), COALESCE(detail, '') "
        "FROM material_dimension_frame_analysis "
        "WHERE video_key = ? AND analysis_state = ? "
        "ORDER BY frame_number, dimension_name"));
    query.addBindValue(videoKey);
    query.addBindValue(static_cast<int>(FrameAnalysisState::Success));
    if (!execQuery(query, errorMessage)) {
        return {};
    }

    while (query.next()) {
        const auto key = query.value(0).toString();
        const auto detail = query.value(5).toString().simplified();
        if (!requestedKeys.contains(key) || detail.isEmpty()) {
            continue;
        }
        DimensionFrameAnalysisRecord record;
        record.dimensionKey = key;
        record.dimensionName = query.value(1).toString();
        record.frameNumber = query.value(2).toInt();
        record.timestampMs = query.value(3).toLongLong();
        record.imagePath = query.value(4).toString();
        record.detail = detail;
        records.append(record);
    }
    return records;
}

QString buildDimensionFrameSummaryContext(const GlobalVideoAsset &asset,
                                          const VisionVideoSummary &summary,
                                          const QVector<DimensionFrameAnalysisRecord> &records)
{
    QStringList parts;
    parts.append(QStringLiteral("文件名：%1").arg(asset.fileName));
    parts.append(QStringLiteral("素材类型：%1").arg(Formatters::assetTypeLabel(asset.assetType)));
    if (!summary.summary.trimmed().isEmpty()) {
        parts.append(QStringLiteral("基础摘要：%1").arg(summary.summary.trimmed()));
    }
    if (!summary.keywords.isEmpty()) {
        parts.append(QStringLiteral("关键词：%1").arg(summary.keywords.join(QStringLiteral("、"))));
    }

    QStringList detailLines;
    int usedChars = 0;
    for (const auto &record : records) {
        const auto line = QStringLiteral("第 %1 帧（%2 ms）/%3：%4")
            .arg(record.frameNumber)
            .arg(record.timestampMs)
            .arg(record.dimensionName, record.detail);
        if (usedChars + line.size() > 12000) {
            break;
        }
        usedChars += line.size();
        detailLines.append(line);
    }
    if (!detailLines.isEmpty()) {
        parts.append(QStringLiteral("逐帧多维度明细（每条来自单帧请求）：\n%1").arg(detailLines.join(QStringLiteral("\n"))));
    }
    if (detailLines.size() < records.size()) {
        parts.append(QStringLiteral("另有 %1 条逐帧明细因汇总上下文限制未直接列入。").arg(records.size() - detailLines.size()));
    }
    return parts.join(QStringLiteral("\n\n"));
}

QString buildDimensionBaseContext(const GlobalVideoAsset &asset,
                                  const VisionVideoSummary &summary,
                                  const QVector<FrameAnalysisRecord> &frames,
                                  const QVector<MaterialDimensionAnalysis> &existingDimensions)
{
    QStringList parts;
    parts.append(QStringLiteral("文件名：%1").arg(asset.fileName));
    parts.append(QStringLiteral("素材类型：%1").arg(Formatters::assetTypeLabel(asset.assetType)));
    if (!asset.technicalSummary.trimmed().isEmpty()) {
        parts.append(QStringLiteral("技术摘要：%1").arg(asset.technicalSummary.trimmed()));
    }
    if (!summary.summary.trimmed().isEmpty()) {
        parts.append(QStringLiteral("基础摘要：%1").arg(summary.summary.trimmed()));
    }
    if (!summary.keywords.isEmpty()) {
        parts.append(QStringLiteral("关键词：%1").arg(summary.keywords.join(QStringLiteral("、"))));
    }
    if (!summary.scenes.isEmpty()) {
        parts.append(QStringLiteral("场景/地点：%1").arg(summary.scenes.join(QStringLiteral("、"))));
    }
    if (!asset.sourceText.trimmed().isEmpty()) {
        parts.append(QStringLiteral("正文/文本预览：%1").arg(asset.sourceText.trimmed().left(8000)));
    }

    QStringList frameLines;
    for (const auto &frame : sampleFramesForContext(successfulFrames(frames), 36)) {
        const auto line = buildFrameContextLine(frame);
        if (!line.isEmpty()) {
            frameLines.append(line);
        }
    }
    if (!frameLines.isEmpty()) {
        parts.append(QStringLiteral("逐帧解析节选：\n%1").arg(frameLines.join(QStringLiteral("\n"))));
    }

    QStringList existingLines;
    for (const auto &dimension : existingDimensions) {
        if (!dimension.name.trimmed().isEmpty() && !dimension.detail.trimmed().isEmpty()) {
            existingLines.append(QStringLiteral("%1：%2").arg(dimension.name.trimmed(), dimension.detail.trimmed()));
        }
    }
    if (!existingLines.isEmpty()) {
        parts.append(QStringLiteral("已有补充维度（不要重复解析）：\n%1").arg(existingLines.join(QStringLiteral("\n"))));
    }
    return parts.join(QStringLiteral("\n\n"));
}

bool persistDimensionAnalyses(QSqlDatabase &db,
                              const QString &videoKey,
                              const QVector<MaterialDimensionAnalysis> &analyses,
                              const QString &modelName,
                              QString *errorMessage)
{
    const auto analyzedAt = nowIso();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO material_dimension_analysis "
        "(video_key, dimension_key, dimension_name, detail, model_name, prompt_version, analyzed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key, dimension_key) DO UPDATE SET "
        "dimension_name = excluded.dimension_name, "
        "detail = excluded.detail, "
        "model_name = excluded.model_name, "
        "prompt_version = excluded.prompt_version, "
        "analyzed_at = excluded.analyzed_at"));

    for (const auto &analysis : analyses) {
        const auto key = dimensionKey(analysis.name);
        const auto detail = analysis.detail.simplified();
        if (key.isEmpty() || detail.isEmpty()) {
            continue;
        }
        query.addBindValue(videoKey);
        query.addBindValue(key);
        query.addBindValue(analysis.name.simplified());
        query.addBindValue(detail);
        query.addBindValue(modelName);
        query.addBindValue(QStringLiteral("v1-dimension-supplement"));
        query.addBindValue(analyzedAt);
        if (!execQuery(query, errorMessage)) {
            return false;
        }
        query.finish();
    }
    return true;
}

void recalculateTaskCounts(const QVector<FrameAnalysisRecord> &frames, VideoAnalysisTask *task)
{
    if (!task) {
        return;
    }

    task->totalFrames = frames.size();
    task->completedFrames = 0;
    task->successfulFrames = 0;
    task->skippedFrames = 0;
    for (const auto &frame : frames) {
        if (frame.analysisState == FrameAnalysisState::Success) {
            ++task->successfulFrames;
            ++task->completedFrames;
        } else if (frame.analysisState == FrameAnalysisState::Skipped) {
            ++task->skippedFrames;
            ++task->completedFrames;
        }
    }
}

void applyFrameAnalysis(FrameAnalysisRecord *frame,
                        const VisionFrameAnalysis &analysis,
                        const QString &modelName)
{
    if (!frame) {
        return;
    }
    frame->caption = analysis.caption;
    frame->tags = analysis.tags;
    frame->objects = analysis.objects;
    frame->actions = analysis.actions;
    frame->setting = analysis.setting;
    frame->entities = analysis.entities;
    frame->ocrText = analysis.ocrText;
    frame->ocrBlocks = analysis.ocrBlocks;
    frame->structuredProfileVersion = analysis.structuredProfileVersion;
    frame->factsComplete = analysis.factsComplete;
    frame->modelName = modelName;
    frame->promptVersion = structuredPromptVersion();
    frame->analyzedAt = nowIso();
}

bool loadVisualAnalysisPlan(QSqlDatabase &db,
                            const QString &videoKey,
                            VisualAnalysisPlan *plan,
                            bool *found,
                            QString *errorMessage)
{
    if (found) {
        *found = false;
    }
    if (!plan) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("视觉采样计划输出为空");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT sampling_policy, frame_interval, structured_profile_version, source_frame_count, planned_frame_count, "
        "asset_size_bytes, asset_modified_at, created_at, updated_at "
        "FROM video_analysis_plan WHERE video_key = ?"));
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    if (!query.next()) {
        *plan = {};
        plan->videoKey = videoKey;
        return true;
    }
    plan->videoKey = videoKey;
    plan->samplingPolicy = query.value(0).toString();
    plan->frameInterval = query.value(1).toInt();
    plan->structuredProfileVersion = query.value(2).toInt();
    plan->sourceFrameCount = query.value(3).toInt();
    plan->plannedFrameCount = query.value(4).toInt();
    plan->assetSizeBytes = query.value(5).toLongLong();
    plan->assetModifiedAt = query.value(6).toString();
    plan->createdAt = query.value(7).toString();
    plan->updatedAt = query.value(8).toString();
    if (found) {
        *found = true;
    }
    return true;
}

bool persistVisualAnalysisPlan(QSqlDatabase &db,
                               const VisualAnalysisPlan &plan,
                               QString *errorMessage)
{
    const auto now = nowIso();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO video_analysis_plan "
        "(video_key, sampling_policy, frame_interval, structured_profile_version, source_frame_count, planned_frame_count, "
        "asset_size_bytes, asset_modified_at, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key) DO UPDATE SET "
        "sampling_policy = excluded.sampling_policy, frame_interval = excluded.frame_interval, "
        "structured_profile_version = excluded.structured_profile_version, source_frame_count = excluded.source_frame_count, "
        "planned_frame_count = excluded.planned_frame_count, asset_size_bytes = excluded.asset_size_bytes, "
        "asset_modified_at = excluded.asset_modified_at, updated_at = excluded.updated_at"));
    query.addBindValue(plan.videoKey);
    query.addBindValue(plan.samplingPolicy);
    query.addBindValue(qMax(1, plan.frameInterval));
    query.addBindValue(plan.structuredProfileVersion);
    query.addBindValue(qMax(0, plan.sourceFrameCount));
    query.addBindValue(qMax(0, plan.plannedFrameCount));
    query.addBindValue(plan.assetSizeBytes);
    query.addBindValue(plan.assetModifiedAt);
    query.addBindValue(plan.createdAt.trimmed().isEmpty() ? now : plan.createdAt);
    query.addBindValue(now);
    return execQuery(query, errorMessage);
}

bool syncFramePlanRows(QSqlDatabase &db,
                       const QString &videoKey,
                       const QVector<ExtractedFrame> &plannedFrames,
                       bool removeUnplanned,
                       QString *errorMessage)
{
    QSet<int> plannedNumbers;
    for (const auto &frame : plannedFrames) {
        plannedNumbers.insert(frame.frameNumber);
    }

    if (removeUnplanned) {
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral("SELECT frame_number FROM video_frame_analysis WHERE video_key = ?"));
        existing.addBindValue(videoKey);
        if (!execQuery(existing, errorMessage)) {
            return false;
        }
        QVector<int> obsolete;
        while (existing.next()) {
            const auto frameNumber = existing.value(0).toInt();
            if (!plannedNumbers.contains(frameNumber)) {
                obsolete.append(frameNumber);
            }
        }
        for (const auto frameNumber : obsolete) {
            QSqlQuery remove(db);
            remove.prepare(QStringLiteral(
                "DELETE FROM video_frame_analysis WHERE video_key = ? AND frame_number = ?"));
            remove.addBindValue(videoKey);
            remove.addBindValue(frameNumber);
            if (!execQuery(remove, errorMessage)) {
                return false;
            }
        }
    }

    for (const auto &frame : plannedFrames) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral(
            "SELECT 1 FROM video_frame_analysis WHERE video_key = ? AND frame_number = ? LIMIT 1"));
        exists.addBindValue(videoKey);
        exists.addBindValue(frame.frameNumber);
        if (!execQuery(exists, errorMessage)) {
            return false;
        }
        const auto rowExists = exists.next();
        if (!rowExists) {
            if (!insertFrameRow(db, videoKey, frame, errorMessage)) {
                return false;
            }
            continue;
        }

        QSqlQuery update(db);
        update.prepare(QStringLiteral(
            "UPDATE video_frame_analysis SET timestamp_ms = ?, image_path = ? "
            "WHERE video_key = ? AND frame_number = ?"));
        update.addBindValue(frame.timestampMs);
        update.addBindValue(frame.imagePath);
        update.addBindValue(videoKey);
        update.addBindValue(frame.frameNumber);
        if (!execQuery(update, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool hasIncompleteVisualFrames(QSqlDatabase &db,
                               const QString &videoKey,
                               int contactSheetFrameCount,
                               QString *errorMessage)
{
    Q_UNUSED(contactSheetFrameCount)
    if (errorMessage) {
        errorMessage->clear();
    }
    VisualAnalysisPlan plan;
    bool found = false;
    if (!loadVisualAnalysisPlan(db, videoKey, &plan, &found, errorMessage)) {
        return true;
    }
    if (!found
        || !VisualAnalysisMetadata::isCurrentSamplingPolicy(plan.samplingPolicy)
        || plan.structuredProfileVersion < cinevault::searchconfig::kStructuredVisionProfileVersion
        || plan.sourceFrameCount <= 0) {
        return true;
    }
    const auto frames = loadFrameRows(db, videoKey, errorMessage);
    if (errorMessage && !errorMessage->trimmed().isEmpty()) {
        return true;
    }
    return std::any_of(frames.cbegin(), frames.cend(), [](const FrameAnalysisRecord &frame) {
        return !VisualAnalysisMetadata::isFrameAnalysisComplete(
            frame,
            cinevault::searchconfig::kStructuredVisionProfileVersion);
    });
}

QString skippedFramesWarning(int skippedFrames)
{
    return skippedFrames > 0
        ? QStringLiteral("已跳过 %1 帧，可手动补解析。").arg(skippedFrames)
        : QString();
}

bool updateAssetState(QSqlDatabase &db,
                      const QString &videoKey,
                      VideoAnalysisStatus analysisStatus,
                      ConfirmationStatus confirmationStatus,
                      const QString &errorMessageText,
                      QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE global_video_asset SET analysis_status = ?, confirmation_status = ?, error_message = ?, updated_at = ? "
        "WHERE video_key = ?"));
    query.addBindValue(static_cast<int>(analysisStatus));
    query.addBindValue(static_cast<int>(confirmationStatus));
    query.addBindValue(errorMessageText);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(videoKey);
    return execQuery(query, errorMessage);
}

bool deleteAnalysisArtifacts(QSqlDatabase &db, const QString &videoKey, bool hasFts5, QString *errorMessage)
{
    QSqlQuery frames(db);
    frames.prepare(QStringLiteral("DELETE FROM video_frame_analysis WHERE video_key = ?"));
    frames.addBindValue(videoKey);
    if (!execQuery(frames, errorMessage)) {
        return false;
    }

    QSqlQuery result(db);
    result.prepare(QStringLiteral("DELETE FROM video_analysis_result WHERE video_key = ?"));
    result.addBindValue(videoKey);
    if (!execQuery(result, errorMessage)) {
        return false;
    }

    if (!deleteAnalysisTask(db, videoKey, errorMessage)) {
        return false;
    }

    QSqlQuery plan(db);
    plan.prepare(QStringLiteral("DELETE FROM video_analysis_plan WHERE video_key = ?"));
    plan.addBindValue(videoKey);
    if (!execQuery(plan, errorMessage)) {
        return false;
    }

    QSqlQuery dimensionFrames(db);
    dimensionFrames.prepare(QStringLiteral("DELETE FROM material_dimension_frame_analysis WHERE video_key = ?"));
    dimensionFrames.addBindValue(videoKey);
    if (!execQuery(dimensionFrames, errorMessage)) {
        return false;
    }

    QSqlQuery dimensions(db);
    dimensions.prepare(QStringLiteral("DELETE FROM material_dimension_analysis WHERE video_key = ?"));
    dimensions.addBindValue(videoKey);
    if (!execQuery(dimensions, errorMessage)) {
        return false;
    }

    if (hasFts5) {
        QSqlQuery fts(db);
        fts.prepare(QStringLiteral("DELETE FROM video_search_fts WHERE video_key = ?"));
        fts.addBindValue(videoKey);
        if (!execQuery(fts, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool insertFrameRow(QSqlDatabase &db, const QString &videoKey, const ExtractedFrame &frame, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO video_frame_analysis "
        "(video_key, frame_number, timestamp_ms, image_path, caption, tags_json, objects_json, actions, setting_text, error_message, "
        "entities_json, ocr_text, ocr_blocks_json, structured_profile_version, facts_complete, model_name, prompt_version, analyzed_at, "
        "analysis_state, retry_count, last_http_status, last_attempt_at) "
        "VALUES (?, ?, ?, ?, '', '[]', '[]', '', '', '', '[]', '', '[]', 1, 0, '', '', '', ?, 0, 0, '')"));
    query.addBindValue(videoKey);
    query.addBindValue(frame.frameNumber);
    query.addBindValue(frame.timestampMs);
    query.addBindValue(frame.imagePath);
    query.addBindValue(static_cast<int>(FrameAnalysisState::Pending));
    return execQuery(query, errorMessage);
}

bool updateFrameAnalysis(QSqlDatabase &db,
                         const QString &videoKey,
                         const FrameAnalysisRecord &frame,
                         QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE video_frame_analysis SET image_path = ?, caption = ?, tags_json = ?, objects_json = ?, "
        "actions = ?, setting_text = ?, entities_json = ?, ocr_text = ?, ocr_blocks_json = ?, "
        "structured_profile_version = ?, facts_complete = ?, model_name = ?, prompt_version = ?, analyzed_at = ?, "
        "error_message = ?, analysis_state = ?, retry_count = ?, last_http_status = ?, last_attempt_at = ? "
        "WHERE video_key = ? AND frame_number = ?"));
    query.addBindValue(frame.imagePath);
    query.addBindValue(frame.caption);
    query.addBindValue(toJson(frame.tags));
    query.addBindValue(toJson(frame.objects));
    query.addBindValue(frame.actions);
    query.addBindValue(frame.setting);
    query.addBindValue(nonNullText(VisualAnalysisMetadata::entityFactsToJson(frame.entities)));
    query.addBindValue(nonNullText(frame.ocrText));
    query.addBindValue(nonNullText(toJson(frame.ocrBlocks)));
    query.addBindValue(frame.structuredProfileVersion);
    query.addBindValue(frame.factsComplete ? 1 : 0);
    query.addBindValue(nonNullText(frame.modelName));
    query.addBindValue(nonNullText(frame.promptVersion));
    query.addBindValue(nonNullText(frame.analyzedAt));
    query.addBindValue(frame.errorMessage);
    query.addBindValue(static_cast<int>(frame.analysisState));
    query.addBindValue(frame.retryCount);
    query.addBindValue(frame.lastHttpStatus);
    query.addBindValue(nonNullText(frame.lastAttemptAt));
    query.addBindValue(videoKey);
    query.addBindValue(frame.frameNumber);
    return execQuery(query, errorMessage);
}

QString buildSearchText(const VisionVideoSummary &summary, const QVector<FrameAnalysisRecord> &frames, const QString &sourceText)
{
    QStringList parts;
    if (!summary.summary.trimmed().isEmpty()) {
        parts.append(summary.summary.trimmed());
    }
    if (!summary.keywords.isEmpty()) {
        parts.append(summary.keywords.join(QStringLiteral(" ")));
    }
    if (!summary.scenes.isEmpty()) {
        parts.append(summary.scenes.join(QStringLiteral(" ")));
    }
    for (const auto &frame : frames) {
        if (!frame.caption.trimmed().isEmpty()) {
            parts.append(frame.caption.trimmed());
        }
        if (!frame.tags.isEmpty()) {
            parts.append(frame.tags.join(QStringLiteral(" ")));
        }
        if (!frame.objects.isEmpty()) {
            parts.append(frame.objects.join(QStringLiteral(" ")));
        }
        if (!frame.actions.trimmed().isEmpty()) {
            parts.append(frame.actions.trimmed());
        }
        if (!frame.setting.trimmed().isEmpty()) {
            parts.append(frame.setting.trimmed());
        }
        const auto entityTerms = VisualAnalysisMetadata::entityFactSearchTerms(frame.entities);
        if (!entityTerms.isEmpty()) {
            parts.append(entityTerms.join(QStringLiteral(" ")));
        }
        if (!frame.ocrText.trimmed().isEmpty()) {
            parts.append(frame.ocrText.trimmed());
        }
    }
    if (!sourceText.trimmed().isEmpty()) {
        parts.append(sourceText.trimmed());
    }
    return parts.join(QStringLiteral(" "));
}

bool upsertSearchFts(QSqlDatabase &db,
                     const GlobalVideoAsset &asset,
                     const VisionVideoSummary &summary,
                     const QVector<FrameAnalysisRecord> &frames,
                     const QString &sourceText,
                     bool hasFts5,
                     QString *errorMessage)
{
    if (!hasFts5) {
        return true;
    }

    QSqlQuery deleteFts(db);
    deleteFts.prepare(QStringLiteral("DELETE FROM video_search_fts WHERE video_key = ?"));
    deleteFts.addBindValue(asset.videoKey);
    if (!execQuery(deleteFts, errorMessage)) {
        return false;
    }

    QStringList frameTexts;
    for (const auto &frame : frames) {
        QStringList frameParts;
        if (!frame.caption.trimmed().isEmpty()) {
            frameParts.append(frame.caption.trimmed());
        }
        if (!frame.tags.isEmpty()) {
            frameParts.append(frame.tags.join(QStringLiteral(" ")));
        }
        if (!frame.objects.isEmpty()) {
            frameParts.append(frame.objects.join(QStringLiteral(" ")));
        }
        if (!frame.actions.trimmed().isEmpty()) {
            frameParts.append(frame.actions.trimmed());
        }
        if (!frame.setting.trimmed().isEmpty()) {
            frameParts.append(frame.setting.trimmed());
        }
        const auto entityTerms = VisualAnalysisMetadata::entityFactSearchTerms(frame.entities);
        if (!entityTerms.isEmpty()) {
            frameParts.append(entityTerms.join(QStringLiteral(" ")));
        }
        if (!frame.ocrText.trimmed().isEmpty()) {
            frameParts.append(frame.ocrText.trimmed());
        }
        if (!frameParts.isEmpty()) {
            frameTexts.append(frameParts.join(QStringLiteral(" ")));
        }
    }

    QSqlQuery insertFts(db);
    insertFts.prepare(QStringLiteral(
        "INSERT INTO video_search_fts "
        "(video_key, project_name, source_root_name, file_name, relative_path, absolute_path, "
        "asset_type_label, extension, technical_summary, summary, keywords, captions, source_text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insertFts.addBindValue(asset.videoKey);
    insertFts.addBindValue(asset.projectName);
    insertFts.addBindValue(asset.sourceRootName);
    insertFts.addBindValue(asset.fileName);
    insertFts.addBindValue(asset.relativePath);
    insertFts.addBindValue(asset.absolutePath);
    insertFts.addBindValue(Formatters::assetTypeLabel(asset.assetType));
    insertFts.addBindValue(asset.extension);
    insertFts.addBindValue(asset.technicalSummary);
    insertFts.addBindValue(summary.summary);
    insertFts.addBindValue(summary.keywords.join(QStringLiteral(" ")));
    insertFts.addBindValue(frameTexts.join(QStringLiteral(" ")));
    insertFts.addBindValue(sourceText);
    return execQuery(insertFts, errorMessage);
}

bool persistSourceTextForSearch(QSqlDatabase &db,
                                const GlobalVideoAsset &asset,
                                const QString &sourceText,
                                bool hasFts5,
                                QString *errorMessage)
{
    const auto updatedAt = nowIso();
    QSqlQuery updateAsset(db);
    updateAsset.prepare(QStringLiteral(
        "UPDATE global_video_asset SET source_text = ?, updated_at = ? WHERE video_key = ?"));
    updateAsset.addBindValue(sourceText);
    updateAsset.addBindValue(updatedAt);
    updateAsset.addBindValue(asset.videoKey);
    if (!execQuery(updateAsset, errorMessage)) {
        return false;
    }

    VisionVideoSummary emptySummary;
    return upsertSearchFts(db, asset, emptySummary, {}, sourceText, hasFts5, errorMessage);
}

bool persistSummary(QSqlDatabase &db,
                    const GlobalVideoAsset &asset,
                    const VisionVideoSummary &summary,
                    const QVector<FrameAnalysisRecord> &frames,
                    const QString &sourceText,
                    bool hasFts5,
                    QString *errorMessage)
{
    const auto analyzedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    const auto searchText = buildSearchText(summary, frames, sourceText);

    QSqlQuery result(db);
    result.prepare(QStringLiteral(
        "INSERT INTO video_analysis_result "
        "(video_key, summary, keywords_json, scenes_json, search_text, model_name, prompt_version, analyzed_at, confirmed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key) DO UPDATE SET "
        "summary = excluded.summary, "
        "keywords_json = excluded.keywords_json, "
        "scenes_json = excluded.scenes_json, "
        "search_text = excluded.search_text, "
        "model_name = excluded.model_name, "
        "prompt_version = excluded.prompt_version, "
        "analyzed_at = excluded.analyzed_at, "
        "confirmed_at = excluded.confirmed_at"));
    result.addBindValue(asset.videoKey);
    result.addBindValue(summary.summary);
    result.addBindValue(toJson(summary.keywords));
    result.addBindValue(toJson(summary.scenes));
    result.addBindValue(searchText);
    result.addBindValue(QStringLiteral("openai-compatible"));
    result.addBindValue(QStringLiteral("v3-structured-entity-ocr-search"));
    result.addBindValue(analyzedAt);
    result.addBindValue(analyzedAt);
    if (!execQuery(result, errorMessage)) {
        return false;
    }

    QSqlQuery updateAsset(db);
    updateAsset.prepare(QStringLiteral(
        "UPDATE global_video_asset SET analysis_status = ?, confirmation_status = ?, source_text = ?, error_message = '', updated_at = ? "
        "WHERE video_key = ?"));
    updateAsset.addBindValue(static_cast<int>(VideoAnalysisStatus::Ready));
    updateAsset.addBindValue(static_cast<int>(ConfirmationStatus::Confirmed));
    updateAsset.addBindValue(sourceText);
    updateAsset.addBindValue(analyzedAt);
    updateAsset.addBindValue(asset.videoKey);
    if (!execQuery(updateAsset, errorMessage)) {
        return false;
    }

    return upsertSearchFts(db, asset, summary, frames, sourceText, hasFts5, errorMessage);
}

AnalysisConfig loadConfig(const AppSettings *settings)
{
    AnalysisConfig config;
    if (!settings) {
        return config;
    }
    config.baseUrl = settings->visionBaseUrl();
    config.apiKey = settings->visionApiKey();
    config.model = settings->visionModel();
    config.extractionStrategy = settings->videoFrameExtractionStrategy();
    config.frameIntervalSeconds = settings->videoFrameIntervalSeconds();
    config.sceneThreshold = settings->videoSceneThreshold();
    config.minimumSharpness = settings->videoMinimumSharpness();
    config.contactSheetFrameCount = settings->contactSheetFrameCount();
    config.timeoutSec = settings->analysisTimeoutSec();
    return config;
}
}

VideoAnalysisService::VideoAnalysisService(GlobalDatabaseManager *globalDatabaseManager,
                                           JobEngine *jobEngine,
                                           AppSettings *settings,
                                           FFmpegAdapter *ffmpegAdapter,
                                           VisionApiClient *visionApiClient,
                                           QObject *parent)
    : QObject(parent)
    , m_globalDatabaseManager(globalDatabaseManager)
    , m_jobEngine(jobEngine)
    , m_settings(settings)
    , m_ffmpegAdapter(ffmpegAdapter)
    , m_visionApiClient(visionApiClient)
{
}

VideoAnalysisService::~VideoAnalysisService()
{
    beginShutdown();
    waitForIdle();
}

void VideoAnalysisService::waitForIdle()
{
    m_futures.waitForFinished();
}

void VideoAnalysisService::cancelPendingWork(const QString &reason)
{
    m_analysisStopSource.request_stop();
    m_dimensionStopSource.request_stop();
    m_analysisQueue.clear();
    m_dimensionAnalysisQueue.clear();
    m_queuedVideoKeys.clear();
    m_dimensionAnalysisKeys.clear();
    m_batchCurrentDetail = reason.trimmed();
    notifyBatchChanged();
    emit analysisQueueChanged(m_currentVideoKey, 0);
}

void VideoAnalysisService::beginShutdown()
{
    if (m_shuttingDown.load()) {
        return;
    }
    m_shuttingDown.store(true);
    cancelPendingWork(QStringLiteral("应用正在退出，未开始的解析任务已保存待恢复"));
}

bool VideoAnalysisService::hasBatchSummary() const
{
    return !m_batchStates.isEmpty();
}

int VideoAnalysisService::batchTotalCount() const
{
    return m_batchStates.size();
}

int VideoAnalysisService::batchFinishedCount() const
{
    int finished = 0;
    for (auto it = m_batchStates.cbegin(); it != m_batchStates.cend(); ++it) {
        if (it.value() == BatchItemState::Completed || it.value() == BatchItemState::Failed) {
            ++finished;
        }
    }
    return finished;
}

int VideoAnalysisService::batchFailedCount() const
{
    int failed = 0;
    for (auto it = m_batchStates.cbegin(); it != m_batchStates.cend(); ++it) {
        if (it.value() == BatchItemState::Failed) {
            ++failed;
        }
    }
    return failed;
}

int VideoAnalysisService::batchSuccessfulCount() const
{
    return qMax(0, batchFinishedCount() - batchFailedCount());
}

int VideoAnalysisService::batchQueuedCount() const
{
    int queued = 0;
    for (auto it = m_batchStates.cbegin(); it != m_batchStates.cend(); ++it) {
        if (it.value() == BatchItemState::Queued) {
            ++queued;
        }
    }
    return queued;
}

qint64 VideoAnalysisService::batchProgressPercent() const
{
    const auto total = batchTotalCount();
    if (total <= 0) {
        return 0;
    }
    const auto currentProgress = m_currentVideoKey.trimmed().isEmpty()
        ? 0
        : qBound<qint64>(0LL, m_batchCurrentProgress, 100LL);
    return qBound<qint64>(0LL,
                          ((static_cast<qint64>(batchFinishedCount()) * 100LL) + currentProgress) / static_cast<qint64>(total),
                          100LL);
}

qint64 VideoAnalysisService::batchCurrentProgressPercent() const
{
    if (m_currentVideoKey.trimmed().isEmpty()) {
        return 0;
    }
    return qBound<qint64>(0LL, m_batchCurrentProgress, 100LL);
}

QString VideoAnalysisService::batchCurrentLabel() const
{
    const auto key = m_currentVideoKey.trimmed();
    if (key.isEmpty()) {
        return QString();
    }
    return m_batchLabels.value(key, key);
}

QString VideoAnalysisService::batchCurrentDetail() const
{
    if (m_currentVideoKey.trimmed().isEmpty()) {
        return QStringLiteral("当前没有正在处理的素材。");
    }

    const auto detail = m_batchCurrentDetail.trimmed();
    return detail.isEmpty() ? QStringLiteral("正在处理当前素材。") : detail;
}

QString VideoAnalysisService::batchStatusText() const
{
    if (!hasBatchSummary()) {
        return QStringLiteral("暂无素材解析批次。");
    }

    const auto total = batchTotalCount();
    const auto finished = batchFinishedCount();
    const auto failed = batchFailedCount();
    const auto queued = batchQueuedCount();
    const auto currentLabel = batchCurrentLabel();

    if (!currentLabel.isEmpty()) {
        return QStringLiteral("当前解析：%1 · 已处理 %2/%3 · 失败 %4 · 排队 %5")
            .arg(currentLabel)
            .arg(finished)
            .arg(total)
            .arg(failed)
            .arg(queued);
    }

    return failed > 0
        ? QStringLiteral("本批次完成：已处理 %1/%2 · 失败 %3")
              .arg(finished)
              .arg(total)
              .arg(failed)
        : QStringLiteral("本批次完成：已处理 %1/%2").arg(finished).arg(total);
}

bool VideoAnalysisService::hasPendingAnalysisWork() const
{
    return m_analysisRunning
        || !m_analysisQueue.isEmpty()
        || !m_dimensionAnalysisQueue.isEmpty();
}

bool VideoAnalysisService::validateReadyForEnqueue(const QString &videoKey, QString *errorMessage) const
{
    if (m_shuttingDown.load()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("应用正在退出，已停止派发新的解析任务。");
        }
        return false;
    }
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先选择一个素材。");
        }
        return false;
    }
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材管理中心数据库未打开，请先同步当前项目。");
        }
        return false;
    }
    if (!m_visionApiClient) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("视觉解析客户端不可用。");
        }
        return false;
    }

    const auto config = loadConfig(m_settings);
    if (config.baseUrl.isEmpty() || config.apiKey.isEmpty() || config.model.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先在设置中完整填写 Base URL、API Key 和模型名，并点击“保存并应用”。");
        }
        return false;
    }
    auto db = m_globalDatabaseManager->database();
    GlobalVideoAsset asset;
    if (!loadVideoAsset(db, normalizedKey, &asset, errorMessage)) {
        return false;
    }
    if (!canAnalyzeAsset(asset.assetType, asset.extension)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("该素材类型当前仅参与索引，不支持内容解析。");
        }
        return false;
    }
    if ((asset.assetType == AssetType::Video || isPhotoshopAsset(asset))
        && (!m_ffmpegAdapter || !m_ffmpegAdapter->isAvailable())) {
        if (errorMessage) {
            *errorMessage = m_ffmpegAdapter
                ? m_ffmpegAdapter->unavailableReason()
                : QStringLiteral("FFmpeg 服务不可用。");
        }
        return false;
    }
    if (m_currentVideoKey == normalizedKey
        || m_queuedVideoKeys.contains(normalizedKey)
        || m_dimensionAnalysisKeys.contains(normalizedKey)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("该素材已在视觉解析队列中。");
        }
        return false;
    }
    return true;
}

bool VideoAnalysisService::enqueueJob(const AnalysisJob &job, QString *errorMessage)
{
    const auto normalizedKey = job.videoKey.trimmed();
    if (!validateReadyForEnqueue(normalizedKey, errorMessage)) {
        return false;
    }
    if (job.mode == AnalysisRunMode::SingleFrame && job.frameNumber <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请选择一个可重解析的失败帧。");
        }
        return false;
    }

    AnalysisJob normalizedJob = job;
    normalizedJob.videoKey = normalizedKey;
    resetBatchSummaryIfIdle();
    ensureBatchItem(normalizedKey);
    setBatchItemState(normalizedKey, BatchItemState::Queued);
    m_analysisQueue.enqueue(normalizedJob);
    m_queuedVideoKeys.insert(normalizedKey);
    const auto detail = normalizedJob.mode == AnalysisRunMode::SingleFrame
        ? QStringLiteral("等待重解析第 %1 帧").arg(normalizedJob.frameNumber)
        : QStringLiteral("等待素材解析队列");
    reportAnalysisProgress(normalizedKey, 0, detail, JobState::Pending);
    emit analysisQueueChanged(m_currentVideoKey, m_analysisQueue.size());
    notifyBatchChanged();
    startNextAnalysis();
    return true;
}

bool VideoAnalysisService::enqueueVideo(const QString &videoKey, QString *errorMessage)
{
    const auto normalizedKey = videoKey.trimmed();
    if (!validateReadyForEnqueue(normalizedKey, errorMessage)) {
        return false;
    }

    GlobalVideoAsset asset;
    auto db = m_globalDatabaseManager->database();
    if (!loadVideoAsset(db, normalizedKey, &asset, errorMessage)) {
        return false;
    }

    AnalysisJob job;
    job.videoKey = normalizedKey;
    if (asset.analysisStatus == VideoAnalysisStatus::Ready) {
        QString completenessError;
        const auto hasVisualGap = (asset.assetType == AssetType::Video || asset.assetType == AssetType::Image)
            && hasIncompleteVisualFrames(
                db,
                normalizedKey,
                asset.assetType == AssetType::Video && m_settings
                    ? m_settings->contactSheetFrameCount()
                    : 0,
                &completenessError);
        if (!completenessError.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = completenessError;
            }
            return false;
        }
        job.mode = hasVisualGap ? AnalysisRunMode::Resume : AnalysisRunMode::Rebuild;
    } else if (asset.analysisStatus == VideoAnalysisStatus::Failed || asset.analysisStatus == VideoAnalysisStatus::Running) {
        job.mode = AnalysisRunMode::Resume;
    } else {
        job.mode = AnalysisRunMode::Initial;
    }
    return enqueueJob(job, errorMessage);
}

int VideoAnalysisService::enqueueVideos(const QStringList &videoKeys, QString *errorMessage)
{
    int accepted = 0;
    QSet<QString> seen;
    QStringList rejectedMessages;
    for (const auto &videoKey : videoKeys) {
        const auto normalizedKey = videoKey.trimmed();
        if (normalizedKey.isEmpty() || seen.contains(normalizedKey)) {
            continue;
        }
        seen.insert(normalizedKey);

        QString rejection;
        if (enqueueVideo(normalizedKey, &rejection)) {
            ++accepted;
        } else if (!rejection.trimmed().isEmpty()) {
            rejectedMessages.append(rejection);
        }
    }

    if (errorMessage) {
        if (accepted > 0) {
            *errorMessage = QStringLiteral("已加入 %1 条素材到解析队列。").arg(accepted);
        } else if (!rejectedMessages.isEmpty()) {
            *errorMessage = rejectedMessages.first();
        } else {
            *errorMessage = QStringLiteral("当前结果中没有可解析的素材。");
        }
    }
    return accepted;
}

int VideoAnalysisService::enqueueVideosForSupplement(const QStringList &videoKeys, QString *message)
{
    int accepted = 0;
    int alreadyComplete = 0;
    QSet<QString> seen;
    QStringList rejectedMessages;

    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        if (message) {
            *message = QStringLiteral("素材管理中心数据库未打开，请先同步当前项目。");
        }
        return 0;
    }

    auto db = m_globalDatabaseManager->database();
    for (const auto &videoKey : videoKeys) {
        const auto normalizedKey = videoKey.trimmed();
        if (normalizedKey.isEmpty() || seen.contains(normalizedKey)) {
            continue;
        }
        seen.insert(normalizedKey);

        GlobalVideoAsset asset;
        QString rejection;
        if (!loadVideoAsset(db, normalizedKey, &asset, &rejection)) {
            if (!rejection.trimmed().isEmpty()) {
                rejectedMessages.append(rejection);
            }
            continue;
        }

        AnalysisJob job;
        job.videoKey = normalizedKey;
        if (asset.analysisStatus == VideoAnalysisStatus::Ready) {
            const bool supportsStructuredFacts = asset.assetType == AssetType::Video
                || asset.assetType == AssetType::Image;
            if (!supportsStructuredFacts) {
                if (enqueueConfiguredDimensionsIfNeeded(normalizedKey, &rejection)) {
                    ++accepted;
                    continue;
                }
                if (!rejection.trimmed().isEmpty()) {
                    rejectedMessages.append(rejection);
                    continue;
                }
                ++alreadyComplete;
                continue;
            }

            QString completenessError;
            const auto hasVisualGap = hasIncompleteVisualFrames(
                db,
                normalizedKey,
                asset.assetType == AssetType::Video && m_settings
                    ? m_settings->contactSheetFrameCount()
                    : 0,
                &completenessError);
            if (!completenessError.trimmed().isEmpty()) {
                rejectedMessages.append(completenessError);
                continue;
            }
            if (!hasVisualGap) {
                if (enqueueConfiguredDimensionsIfNeeded(normalizedKey, &rejection)) {
                    ++accepted;
                    continue;
                }
                if (!rejection.trimmed().isEmpty()) {
                    rejectedMessages.append(rejection);
                    continue;
                }
                ++alreadyComplete;
                continue;
            }
            job.mode = AnalysisRunMode::Resume;
        } else if (asset.analysisStatus == VideoAnalysisStatus::Failed
                   || asset.analysisStatus == VideoAnalysisStatus::Running) {
            job.mode = AnalysisRunMode::Resume;
        } else {
            job.mode = AnalysisRunMode::Initial;
        }

        if (enqueueJob(job, &rejection)) {
            ++accepted;
        } else if (!rejection.trimmed().isEmpty()) {
            rejectedMessages.append(rejection);
        }
    }

    if (message) {
        if (accepted > 0) {
            *message = alreadyComplete > 0
                ? QStringLiteral("已加入 %1 条素材进行补充解析，跳过 %2 条结构化维度已完整素材。")
                      .arg(accepted)
                      .arg(alreadyComplete)
                : QStringLiteral("已加入 %1 条素材进行补充解析。").arg(accepted);
        } else if (alreadyComplete > 0 && rejectedMessages.isEmpty()) {
            *message = QStringLiteral("当前结果的结构化解析维度已完整，无需补充解析。");
        } else if (!rejectedMessages.isEmpty()) {
            *message = rejectedMessages.first();
        } else {
            *message = QStringLiteral("当前结果中没有需要补充解析的素材。");
        }
    }
    return accepted;
}

bool VideoAnalysisService::enqueueConfiguredDimensionsIfNeeded(const QString &videoKey,
                                                                QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const auto dimensions = m_settings ? m_settings->customAnalysisDimensions() : QStringList{};
    if (dimensions.isEmpty()) {
        return false;
    }

    QString pendingError;
    const auto pending = pendingDimensionCount(videoKey, dimensions, &pendingError);
    if (pending < 0) {
        if (errorMessage) {
            *errorMessage = pendingError;
        }
        return false;
    }
    if (pending == 0) {
        return false;
    }
    return analyzeDimensions(videoKey, dimensions, errorMessage);
}

int VideoAnalysisService::enqueueVideosForRebuild(const QStringList &videoKeys, QString *message)
{
    int accepted = 0;
    QSet<QString> seen;
    QStringList rejectedMessages;
    for (const auto &videoKey : videoKeys) {
        const auto normalizedKey = videoKey.trimmed();
        if (normalizedKey.isEmpty() || seen.contains(normalizedKey)) {
            continue;
        }
        seen.insert(normalizedKey);

        QString rejection;
        if (enqueueJob(AnalysisJob{normalizedKey, AnalysisRunMode::Rebuild, 0}, &rejection)) {
            ++accepted;
        } else if (!rejection.trimmed().isEmpty()) {
            rejectedMessages.append(rejection);
        }
    }

    if (message) {
        if (accepted > 0) {
            *message = QStringLiteral("已加入 %1 条素材进行全部重新解析。").arg(accepted);
        } else if (!rejectedMessages.isEmpty()) {
            *message = rejectedMessages.first();
        } else {
            *message = QStringLiteral("当前结果中没有可重新解析的素材。");
        }
    }
    return accepted;
}

bool VideoAnalysisService::retryFrame(const QString &videoKey, int frameNumber, QString *errorMessage)
{
    const auto normalizedKey = videoKey.trimmed();
    if (!validateReadyForEnqueue(normalizedKey, errorMessage)) {
        return false;
    }

    GlobalVideoAsset asset;
    auto db = m_globalDatabaseManager->database();
    if (!loadVideoAsset(db, normalizedKey, &asset, errorMessage)) {
        return false;
    }
    if (asset.assetType != AssetType::Video) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("只有视频素材支持重解析单帧。");
        }
        return false;
    }

    QSqlQuery query(m_globalDatabaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT 1 FROM video_frame_analysis WHERE video_key = ? AND frame_number = ? LIMIT 1"));
    query.addBindValue(normalizedKey);
    query.addBindValue(frameNumber);
    if (!execQuery(query, errorMessage) || !query.next()) {
        if (errorMessage && errorMessage->trimmed().isEmpty()) {
            *errorMessage = QStringLiteral("当前帧不存在，无法重解析。");
        }
        return false;
    }

    return enqueueJob(AnalysisJob{normalizedKey, AnalysisRunMode::SingleFrame, frameNumber}, errorMessage);
}

int VideoAnalysisService::pendingDimensionCount(const QString &videoKey,
                                                const QStringList &dimensions,
                                                QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先选择一个素材。");
        }
        return -1;
    }
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材管理中心尚未准备好。");
        }
        return -1;
    }

    const auto requestedDimensions = normalizedDimensionNames(dimensions);
    if (requestedDimensions.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请至少添加一个解析维度。");
        }
        return -1;
    }

    auto db = m_globalDatabaseManager->database();
    const auto existingDimensions = loadDimensionAnalyses(db, normalizedKey, errorMessage);
    if (errorMessage && !errorMessage->trimmed().isEmpty()) {
        return -1;
    }
    return filterNewDimensions(requestedDimensions, existingDimensions).size();
}

bool VideoAnalysisService::analyzeDimensions(const QString &videoKey,
                                             const QStringList &dimensions,
                                             QString *errorMessage)
{
    if (m_shuttingDown.load()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("应用正在退出，已停止派发新的解析任务。");
        }
        return false;
    }
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先选择一个素材。");
        }
        return false;
    }
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen() || !m_visionApiClient) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材管理中心尚未准备好。");
        }
        return false;
    }

    const auto requestedDimensions = normalizedDimensionNames(dimensions);
    if (requestedDimensions.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请至少添加一个解析维度。");
        }
        return false;
    }
    if (m_dimensionAnalysisKeys.contains(normalizedKey)
        || m_currentVideoKey == normalizedKey
        || m_queuedVideoKeys.contains(normalizedKey)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("该素材已在视觉解析队列中。");
        }
        return false;
    }

    const auto config = loadConfig(m_settings);
    if (config.baseUrl.isEmpty() || config.apiKey.isEmpty() || config.model.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("视觉接口参数不完整，请在设置中保存并应用后重试。");
        }
        return false;
    }

    m_dimensionAnalysisKeys.insert(normalizedKey);
    m_dimensionAnalysisQueue.enqueue(DimensionAnalysisJob{normalizedKey, requestedDimensions});
    emit dimensionAnalysisProgressChanged(normalizedKey, true, QStringLiteral("等待多维度解析队列"), QString());
    startNextAnalysis();
    return true;
}

bool VideoAnalysisService::startDimensionAnalysisNow(const QString &videoKey,
                                                     const QStringList &dimensions,
                                                     QString *errorMessage)
{
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先选择一个素材。");
        }
        return false;
    }
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen() || !m_visionApiClient) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材管理中心尚未准备好。");
        }
        return false;
    }

    const auto requestedDimensions = normalizedDimensionNames(dimensions);
    if (requestedDimensions.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请至少添加一个解析维度。");
        }
        return false;
    }
    if (m_dimensionAnalysisKeys.contains(normalizedKey)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("该素材正在进行多维度解析。");
        }
        return false;
    }

    const auto config = loadConfig(m_settings);
    if (config.baseUrl.isEmpty() || config.apiKey.isEmpty() || config.model.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("视觉接口参数不完整，请在设置中保存并应用后重试。");
        }
        return false;
    }

    m_dimensionAnalysisKeys.insert(normalizedKey);
    emit dimensionAnalysisProgressChanged(normalizedKey, true, QStringLiteral("准备多维度解析"), QString());

    const auto jobProjectDatabasePath = m_jobEngine
        ? m_jobEngine->currentProjectDatabasePath()
        : QString();
    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::ContentAnalysis,
                                 QStringLiteral("素材多维度解析"),
                                 QStringLiteral("准备多维度解析"),
                                 0,
                                 analysisSubjectForKey(normalizedKey, lookupVideoLabel(normalizedKey)),
                                 analysisProgressContext(1, 4, QStringLiteral("准备多维度解析")))
        : 0;

    m_dimensionStopSource = std::stop_source{};
    const auto stopToken = m_dimensionStopSource.get_token();
    auto future = QtConcurrent::run([this, normalizedKey, requestedDimensions, config, jobProjectDatabasePath, jobId, stopToken]() {
        const auto connectionName = QStringLiteral("dimension_analysis_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QString errorMessage;
        QSqlDatabase db;
        const auto connectionGuard = qScopeGuard([&]() {
            db.close();
            db = QSqlDatabase();
            m_globalDatabaseManager->closeThreadConnection(connectionName);
        });

        auto report = [&](const QString &detail, qint64 progress, const JobProgressContext &progressContext) {
            updateJob(jobProjectDatabasePath, jobId, progress, detail, progressContext);
            QMetaObject::invokeMethod(this, [this, normalizedKey, detail]() {
                emit dimensionAnalysisProgressChanged(normalizedKey, true, detail, QString());
            }, Qt::QueuedConnection);
        };

        auto finish = [&](bool success, const QString &detail, const QString &error) {
            if (success) {
                completeJob(jobProjectDatabasePath, jobId, detail);
            } else {
                failJob(jobProjectDatabasePath, jobId, error.trimmed().isEmpty() ? detail : error);
            }
            QMetaObject::invokeMethod(this, [this, normalizedKey, success, detail, error]() {
                m_dimensionAnalysisKeys.remove(normalizedKey);
                emit dimensionAnalysisProgressChanged(normalizedKey, false, detail, error);
                if (success) {
                    emit catalogChanged();
                    emit searchDocumentChanged(normalizedKey);
                }
                m_analysisRunning = false;
                emit analysisQueueChanged(m_currentVideoKey, m_analysisQueue.size());
                startNextAnalysis();
            }, Qt::QueuedConnection);
        };

        const auto cancelled = [&]() {
            if (!stopToken.stop_requested() && !m_shuttingDown.load()) {
                return false;
            }
            finish(false, QStringLiteral("多维度解析已取消"), QStringLiteral("任务已取消"));
            return true;
        };

        db = m_globalDatabaseManager->openThreadConnection(connectionName, &errorMessage);
        if (!db.isOpen()) {
            finish(false, QStringLiteral("多维度解析失败"), errorMessage);
            return;
        }
        if (cancelled()) return;

        report(QStringLiteral("正在读取基础解析结果"),
               10,
               analysisProgressContext(1, 4, QStringLiteral("读取基础解析")));
        GlobalVideoAsset asset;
        if (!loadVideoAsset(db, normalizedKey, &asset, &errorMessage)) {
            finish(false, QStringLiteral("多维度解析失败"), errorMessage);
            return;
        }
        updateJobSubject(jobProjectDatabasePath, jobId, analysisSubjectForAsset(asset));
        if (asset.analysisStatus != VideoAnalysisStatus::Ready) {
            finish(false,
                   QStringLiteral("请先完成基础内容解析。"),
                   QStringLiteral("多维度解析需要基于已完成的基础解析结果。"));
            return;
        }

        VisionVideoSummary summary;
        if (!loadAnalysisSummary(db, normalizedKey, &summary, &errorMessage)) {
            finish(false, QStringLiteral("多维度解析失败"), errorMessage);
            return;
        }

        const auto frames = loadFrameRows(db, normalizedKey, &errorMessage);
        if (!errorMessage.trimmed().isEmpty()) {
            finish(false, QStringLiteral("多维度解析失败"), errorMessage);
            return;
        }

        const auto existingDimensions = loadDimensionAnalyses(db, normalizedKey, &errorMessage);
        if (!errorMessage.trimmed().isEmpty()) {
            finish(false, QStringLiteral("多维度解析失败"), errorMessage);
            return;
        }

        const auto pendingDimensions = filterNewDimensions(requestedDimensions, existingDimensions);
        if (pendingDimensions.isEmpty()) {
            finish(true,
                   QStringLiteral("所选维度已存在，无需重复解析。"),
                   QString());
            return;
        }

        int httpStatusCode = 0;
        QString analysisError;
        std::optional<QVector<MaterialDimensionAnalysis>> analyses;
        if (asset.assetType == AssetType::Video) {
            const auto usableFrames = successfulFrames(frames);
            if (usableFrames.isEmpty()) {
                finish(false,
                       QStringLiteral("多维度解析失败"),
                       QStringLiteral("没有可用于逐帧多维度解析的成功帧。"));
                return;
            }

            auto completedFrames = loadCompletedDimensionFrames(db, normalizedKey, pendingDimensions, &errorMessage);
            if (!errorMessage.trimmed().isEmpty()) {
                finish(false, QStringLiteral("多维度解析失败"), errorMessage);
                return;
            }

            int totalFrameRequests = 0;
            for (const auto &frame : usableFrames) {
                for (const auto &dimension : pendingDimensions) {
                    if (!completedFrames.value(dimensionKey(dimension)).contains(frame.frameNumber)) {
                        ++totalFrameRequests;
                        break;
                    }
                }
            }

            int frameRequestIndex = 0;
            for (const auto &frame : usableFrames) {
                if (cancelled()) return;
                QStringList framePendingDimensions;
                for (const auto &dimension : pendingDimensions) {
                    if (!completedFrames.value(dimensionKey(dimension)).contains(frame.frameNumber)) {
                        framePendingDimensions.append(dimension);
                    }
                }
                if (framePendingDimensions.isEmpty()) {
                    continue;
                }

                ++frameRequestIndex;
                const auto frameTotal = qMax(1, totalFrameRequests);
                const auto frameProgress = 20 + (static_cast<qint64>(frameRequestIndex) * 55) / frameTotal;
                report(QStringLiteral("逐帧多维度解析 %1/%2：第 %3 帧")
                           .arg(frameRequestIndex)
                           .arg(frameTotal)
                           .arg(frame.frameNumber),
                       frameProgress,
                       analysisProgressContext(2,
                                               4,
                                               QStringLiteral("逐帧多维度解析"),
                                               frameRequestIndex,
                                               frameTotal,
                                               QStringLiteral("帧"),
                                               frame.frameNumber));

                if (frame.imagePath.trimmed().isEmpty() || !QFileInfo::exists(frame.imagePath)) {
                    const auto missingImageError = QStringLiteral("帧图不存在，无法执行单帧多维度解析。");
                    if (!persistDimensionFrameFailures(db,
                                                       normalizedKey,
                                                       frame,
                                                       framePendingDimensions,
                                                       config.model,
                                                       missingImageError,
                                                       &errorMessage)) {
                        finish(false, QStringLiteral("多维度解析失败"), errorMessage);
                        return;
                    }
                    continue;
                }

                QString frameError;
                int frameHttpStatusCode = 0;
                const auto frameAnalyses = m_visionApiClient->analyzeFrameDimensions(frame.imagePath,
                                                                                    asset.fileName,
                                                                                    buildFrameDimensionContext(frame),
                                                                                    framePendingDimensions,
                                                                                    config.baseUrl,
                                                                                    config.apiKey,
                                                                                    config.model,
                                                                                    config.timeoutSec,
                                                                                    &frameError,
                                                                                    &frameHttpStatusCode);
                if (frameAnalyses.has_value()) {
                    if (!persistDimensionFrameAnalyses(db, normalizedKey, frame, *frameAnalyses, config.model, &errorMessage)) {
                        finish(false, QStringLiteral("多维度解析失败"), errorMessage);
                        return;
                    }
                    for (const auto &analysis : *frameAnalyses) {
                        const auto key = dimensionKey(analysis.name);
                        if (!key.isEmpty()) {
                            completedFrames[key].insert(frame.frameNumber);
                        }
                    }
                } else {
                    const auto errorText = frameError.trimmed().isEmpty()
                        ? QStringLiteral("单帧多维度解析失败。")
                        : frameError.trimmed();
                    if (!persistDimensionFrameFailures(db,
                                                       normalizedKey,
                                                       frame,
                                                       framePendingDimensions,
                                                       config.model,
                                                       errorText,
                                                       &errorMessage)) {
                        finish(false, QStringLiteral("多维度解析失败"), errorMessage);
                        return;
                    }
                }
            }

            const auto frameDimensionAnalyses = loadDimensionFrameAnalyses(db, normalizedKey, pendingDimensions, &errorMessage);
            if (!errorMessage.trimmed().isEmpty()) {
                finish(false, QStringLiteral("多维度解析失败"), errorMessage);
                return;
            }
            if (frameDimensionAnalyses.isEmpty()) {
                finish(false,
                       QStringLiteral("多维度解析失败"),
                       QStringLiteral("逐帧多维度解析没有生成可用于汇总的明细。"));
                return;
            }

            const auto summaryContext = buildDimensionFrameSummaryContext(asset, summary, frameDimensionAnalyses);
            report(QStringLiteral("正在汇总 %1 条逐帧多维度明细").arg(frameDimensionAnalyses.size()),
                   80,
                   analysisProgressContext(3,
                                           4,
                                           QStringLiteral("汇总多维度结果"),
                                           frameDimensionAnalyses.size(),
                                           frameDimensionAnalyses.size(),
                                           QStringLiteral("条明细")));
            if (cancelled()) return;
            analyses = m_visionApiClient->analyzeDimensions(asset.fileName,
                                                            summaryContext,
                                                            pendingDimensions,
                                                            config.baseUrl,
                                                            config.apiKey,
                                                            config.model,
                                                            config.timeoutSec,
                                                            &analysisError,
                                                            &httpStatusCode);
        } else {
            const auto baseContext = buildDimensionBaseContext(asset, summary, frames, existingDimensions);
            report(QStringLiteral("正在补充 %1 个维度").arg(pendingDimensions.size()),
                   55,
                   analysisProgressContext(2,
                                           4,
                                           QStringLiteral("补充维度解析"),
                                           pendingDimensions.size(),
                                           pendingDimensions.size(),
                                           QStringLiteral("个维度")));
            if (cancelled()) return;
            analyses = m_visionApiClient->analyzeDimensions(asset.fileName,
                                                            baseContext,
                                                            pendingDimensions,
                                                            config.baseUrl,
                                                            config.apiKey,
                                                            config.model,
                                                            config.timeoutSec,
                                                            &analysisError,
                                                            &httpStatusCode);
        }
        if (!analyses.has_value()) {
            finish(false, QStringLiteral("多维度解析失败"), analysisError);
            return;
        }

        updateJob(jobProjectDatabasePath,
                  jobId,
                  90,
                  QStringLiteral("正在保存多维度解析结果"),
                  analysisProgressContext(4, 4, QStringLiteral("保存解析结果")));
        if (!db.transaction()) {
            finish(false, QStringLiteral("多维度解析失败"), db.lastError().text());
            return;
        }
        if (!persistDimensionAnalyses(db, normalizedKey, *analyses, config.model, &errorMessage)) {
            db.rollback();
            finish(false, QStringLiteral("多维度解析失败"), errorMessage);
            return;
        }
        if (!db.commit()) {
            finish(false, QStringLiteral("多维度解析失败"), db.lastError().text());
            return;
        }

        updateJob(jobProjectDatabasePath,
                  jobId,
                  100,
                  QStringLiteral("多维度解析完成，新增 %1 个维度。").arg(analyses->size()),
                  analysisProgressContext(4, 4, QStringLiteral("完成多维度解析"), analyses->size(), analyses->size(), QStringLiteral("个维度")));
        finish(true, QStringLiteral("多维度解析完成，新增 %1 个维度。").arg(analyses->size()), QString());
    });
    m_futures.addFuture(future);
    return true;
}

void VideoAnalysisService::startNextDimensionAnalysis()
{
    if (m_analysisRunning || m_dimensionAnalysisQueue.isEmpty()) {
        return;
    }

    const auto job = m_dimensionAnalysisQueue.dequeue();
    m_analysisRunning = true;
    m_dimensionAnalysisKeys.remove(job.videoKey);

    QString errorMessage;
    if (startDimensionAnalysisNow(job.videoKey, job.dimensions, &errorMessage)) {
        return;
    }

    m_dimensionAnalysisKeys.remove(job.videoKey);
    m_analysisRunning = false;
    emit dimensionAnalysisProgressChanged(job.videoKey,
                                          false,
                                          errorMessage.trimmed().isEmpty() ? QStringLiteral("多维度解析失败") : errorMessage,
                                          errorMessage);
    emit analysisQueueChanged(m_currentVideoKey, m_analysisQueue.size());
    startNextAnalysis();
}

void VideoAnalysisService::analyzeVideo(const QString &videoKey)
{
    QString errorMessage;
    enqueueVideo(videoKey, &errorMessage);
}

void VideoAnalysisService::startNextAnalysis()
{
    if (m_analysisRunning) {
        return;
    }
    if (m_analysisQueue.isEmpty()) {
        if (!m_dimensionAnalysisQueue.isEmpty()) {
            startNextDimensionAnalysis();
            return;
        }
        emit analysisQueueChanged(QString(), 0);
        return;
    }

    const auto job = m_analysisQueue.dequeue();
    m_queuedVideoKeys.remove(job.videoKey);
    m_currentJob = job;
    m_currentVideoKey = job.videoKey;
    m_analysisRunning = true;
    ensureBatchItem(job.videoKey);
    setBatchItemState(job.videoKey, BatchItemState::Running);
    m_batchCurrentProgress = 0;
    m_batchCurrentDetail = job.mode == AnalysisRunMode::SingleFrame
        ? QStringLiteral("准备重解析失败视频帧")
        : QStringLiteral("准备解析素材内容");
    emit analysisQueueChanged(m_currentVideoKey, m_analysisQueue.size());
    notifyBatchChanged();

    const auto config = loadConfig(m_settings);
    if (config.baseUrl.isEmpty() || config.apiKey.isEmpty() || config.model.isEmpty()) {
        const auto errorMessage = QStringLiteral("视觉接口参数不完整，请在设置中保存并应用后重试。");
        reportAnalysisProgress(job.videoKey, 0, errorMessage, JobState::Failed, errorMessage);
        finishCurrentAnalysis(job.videoKey, false);
        return;
    }

    const auto jobProjectDatabasePath = m_jobEngine
        ? m_jobEngine->currentProjectDatabasePath()
        : QString();
    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::ContentAnalysis,
                                 job.mode == AnalysisRunMode::SingleFrame ? QStringLiteral("视频帧补解析") : QStringLiteral("素材内容解析"),
                                 job.mode == AnalysisRunMode::SingleFrame ? QStringLiteral("准备重解析失败视频帧") : QStringLiteral("准备解析素材内容"),
                                 0,
                                 analysisSubjectForKey(job.videoKey, lookupVideoLabel(job.videoKey)),
                                 job.mode == AnalysisRunMode::SingleFrame
                                     ? analysisProgressContext(1, 2, QStringLiteral("准备重解析帧"), 0, 1, QStringLiteral("帧"), job.frameNumber)
                                     : analysisProgressContext(1, 4, QStringLiteral("准备解析素材")))
        : 0;
    reportAnalysisProgress(job.videoKey,
                           0,
                           job.mode == AnalysisRunMode::SingleFrame ? QStringLiteral("准备重解析失败视频帧") : QStringLiteral("准备解析素材内容"),
                           JobState::Running);

    m_analysisStopSource = std::stop_source{};
    const auto stopToken = m_analysisStopSource.get_token();
    auto future = QtConcurrent::run([this, job, config, jobProjectDatabasePath, jobId, stopToken]() {
        const auto connectionName = QStringLiteral("video_analysis_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QString errorMessage;
        qint64 lastProgress = 0;
        QSqlDatabase db;
        bool preserveActiveAnalysisOnFailure = false;
        bool stagingFreshRun = false;
        QString stagingCacheDirectory;
        VideoAnalysisStatus previousAnalysisStatus = VideoAnalysisStatus::Pending;
        ConfirmationStatus previousConfirmationStatus = ConfirmationStatus::Pending;
        QString previousAnalysisError;
        VisualAnalysisPlan stagedPlan;
        const auto connectionGuard = qScopeGuard([&]() {
            db.close();
            db = QSqlDatabase();
            m_globalDatabaseManager->closeThreadConnection(connectionName);
        });

        auto finishFailure = [&](const QString &message, QSqlDatabase *db = nullptr, bool updateAsset = true) {
            const auto normalizedMessage = message.trimmed().isEmpty()
                ? QStringLiteral("解析失败。")
                : message.trimmed();
            if ((updateAsset || preserveActiveAnalysisOnFailure) && db && db->isOpen()) {
                if (preserveActiveAnalysisOnFailure) {
                    updateAssetState(*db,
                                     job.videoKey,
                                     previousAnalysisStatus,
                                     previousConfirmationStatus,
                                     previousAnalysisError,
                                     nullptr);
                } else {
                    updateAssetState(*db,
                                     job.videoKey,
                                     VideoAnalysisStatus::Failed,
                                     ConfirmationStatus::Pending,
                                     normalizedMessage,
                                     nullptr);
                }
            }
            if (preserveActiveAnalysisOnFailure && !stagingCacheDirectory.isEmpty()) {
                QDir(stagingCacheDirectory).removeRecursively();
            }
            failJob(jobProjectDatabasePath, jobId, normalizedMessage);
            reportAnalysisProgress(job.videoKey, lastProgress, normalizedMessage, JobState::Failed, normalizedMessage);
            notifyCatalogChanged(job.videoKey);
            QMetaObject::invokeMethod(this, [this, videoKey = job.videoKey]() {
                finishCurrentAnalysis(videoKey, false);
            }, Qt::QueuedConnection);
        };

        auto updateRunning = [&](qint64 progress, const QString &detail, const JobProgressContext &progressContext = JobProgressContext()) {
            lastProgress = progress;
            updateJob(jobProjectDatabasePath, jobId, progress, detail, progressContext);
            reportAnalysisProgress(job.videoKey, progress, detail, JobState::Running);
        };

        const auto cancelled = [&]() {
            if (!stopToken.stop_requested() && !m_shuttingDown.load()) {
                return false;
            }
            finishFailure(QStringLiteral("解析任务已取消"), db.isOpen() ? &db : nullptr);
            return true;
        };

        auto finishSuccess = [&](const QString &successMessage) {
            completeJob(jobProjectDatabasePath, jobId, successMessage);
            reportAnalysisProgress(job.videoKey, 100, successMessage, JobState::Completed);
            notifyCatalogChanged(job.videoKey);
            QMetaObject::invokeMethod(this, [this, videoKey = job.videoKey]() {
                finishCurrentAnalysis(videoKey, true);
            }, Qt::QueuedConnection);
        };

        db = m_globalDatabaseManager->openThreadConnection(connectionName, &errorMessage);
        if (!db.isOpen()) {
            finishFailure(errorMessage, nullptr, false);
            return;
        }
        if (cancelled()) return;

        GlobalVideoAsset asset;
        if (!loadVideoAsset(db, job.videoKey, &asset, &errorMessage)) {
            finishFailure(errorMessage, &db, false);
            return;
        }
        previousAnalysisStatus = asset.analysisStatus;
        previousConfirmationStatus = asset.confirmationStatus;
        previousAnalysisError = asset.errorMessage;
        updateJobSubject(jobProjectDatabasePath, jobId, analysisSubjectForAsset(asset));

        if (!canAnalyzeAsset(asset.assetType, asset.extension)) {
            const auto message = QStringLiteral("该素材类型当前仅参与索引，不支持内容解析。");
            updateAssetState(db, job.videoKey, VideoAnalysisStatus::IndexedOnly, ConfirmationStatus::Pending, message, nullptr);
            finishFailure(message, &db, false);
            return;
        }

        if (asset.assetType == AssetType::Image) {
            if (!updateAssetState(db, job.videoKey, VideoAnalysisStatus::Running, ConfirmationStatus::Pending, QString(), &errorMessage)) {
                finishFailure(errorMessage, &db, false);
                return;
            }

            updateRunning(20,
                           QStringLiteral("正在转换图片为 JPG 并提交视觉解析"),
                           analysisProgressContext(1, 3, QStringLiteral("提交图片解析"), 0, 1, QStringLiteral("张")));
            auto analysisImagePath = asset.absolutePath;
            if (isPhotoshopAsset(asset)) {
                ThumbnailRequest conversionRequest;
                conversionRequest.assetId = asset.assetId;
                conversionRequest.sourcePath = asset.absolutePath;
                conversionRequest.cachePath = QDir(Paths::projectFrameCacheDirectory(
                                                        asset.projectDatabasePath,
                                                        job.videoKey))
                                                  .filePath(QStringLiteral("photoshop_preview.jpg"));
                conversionRequest.assetType = AssetType::Image;
                conversionRequest.frameIndex = 1;
                conversionRequest.maxWidth = 4096;
                conversionRequest.maxHeight = 4096;
                const auto conversion = m_ffmpegAdapter->generateThumbnail(conversionRequest);
                if (!conversion.success) {
                    const auto message = QStringLiteral("PSD 文件转换为 JPG 失败：%1")
                                             .arg(conversion.errorMessage);
                    updateAssetState(db,
                                     job.videoKey,
                                     VideoAnalysisStatus::Failed,
                                     ConfirmationStatus::Pending,
                                     message,
                                     nullptr);
                    finishFailure(message, &db, false);
                    return;
                }
                analysisImagePath = conversion.outputPath;
            }
            int httpStatusCode = 0;
            QString imageError;
            if (cancelled()) return;
            const auto analysis = m_visionApiClient->analyzeFrame(analysisImagePath,
                                                                  asset.fileName,
                                                                  config.baseUrl,
                                                                  config.apiKey,
                                                                  config.model,
                                                                  config.timeoutSec,
                                                                  &imageError,
                                                                  &httpStatusCode);
            if (!analysis.has_value()) {
                updateAssetState(db, job.videoKey, VideoAnalysisStatus::Failed, ConfirmationStatus::Pending, imageError, nullptr);
                finishFailure(imageError, &db, false);
                return;
            }

            FrameAnalysisRecord imageFrame;
            imageFrame.videoKey = job.videoKey;
            imageFrame.frameNumber = 1;
            imageFrame.imagePath = analysisImagePath;
            imageFrame.retryCount = 1;
            imageFrame.lastHttpStatus = httpStatusCode;
            imageFrame.lastAttemptAt = nowIso();
            imageFrame.analysisState = FrameAnalysisState::Success;
            applyFrameAnalysis(&imageFrame, *analysis, config.model);

            VisionVideoSummary imageSummary;
            imageSummary.summary = analysis->caption;
            imageSummary.scenes = analysis->setting.trimmed().isEmpty()
                ? QStringList{}
                : QStringList{analysis->setting.trimmed()};
            QSet<QString> keywordKeys;
            const auto appendKeywords = [&](const QStringList &items) {
                for (const auto &item : items) {
                    const auto normalized = item.simplified();
                    const auto key = normalized.toCaseFolded();
                    if (!normalized.isEmpty() && !keywordKeys.contains(key)) {
                        keywordKeys.insert(key);
                        imageSummary.keywords.append(normalized);
                    }
                }
            };
            appendKeywords(analysis->tags);
            appendKeywords(analysis->objects);
            appendKeywords(VisualAnalysisMetadata::entityFactSearchTerms(analysis->entities));
            appendKeywords(analysis->ocrBlocks);

            VisualAnalysisPlan imagePlan;
            imagePlan.videoKey = job.videoKey;
            imagePlan.samplingPolicy = QStringLiteral("single_image_v1");
            imagePlan.frameInterval = 1;
            imagePlan.structuredProfileVersion = cinevault::searchconfig::kStructuredVisionProfileVersion;
            imagePlan.sourceFrameCount = 1;
            imagePlan.plannedFrameCount = 1;
            imagePlan.assetSizeBytes = asset.sizeBytes;
            imagePlan.assetModifiedAt = asset.modifiedAt;
            imagePlan.createdAt = nowIso();

            updateRunning(85,
                          QStringLiteral("正在保存图片解析结果"),
                          analysisProgressContext(2, 3, QStringLiteral("保存解析结果"), 1, 1, QStringLiteral("张")));
            if (!db.transaction()) {
                finishFailure(db.lastError().text(), &db);
                return;
            }
            QSqlQuery clearFrames(db);
            clearFrames.prepare(QStringLiteral("DELETE FROM video_frame_analysis WHERE video_key = ?"));
            clearFrames.addBindValue(job.videoKey);
            QSqlQuery clearPlan(db);
            clearPlan.prepare(QStringLiteral("DELETE FROM video_analysis_plan WHERE video_key = ?"));
            clearPlan.addBindValue(job.videoKey);
            const ExtractedFrame imagePlanFrame{1, 0, analysisImagePath};
            if (!execQuery(clearFrames, &errorMessage)
                || !execQuery(clearPlan, &errorMessage)
                || !insertFrameRow(db, job.videoKey, imagePlanFrame, &errorMessage)
                || !updateFrameAnalysis(db, job.videoKey, imageFrame, &errorMessage)
                || !persistVisualAnalysisPlan(db, imagePlan, &errorMessage)
                || !persistSummary(db,
                                   asset,
                                   imageSummary,
                                   QVector<FrameAnalysisRecord>{imageFrame},
                                   QStringLiteral(""),
                                   m_globalDatabaseManager->hasFts5(),
                                   &errorMessage)) {
                db.rollback();
                finishFailure(errorMessage, &db, false);
                return;
            }
            if (!db.commit()) {
                finishFailure(db.lastError().text(), &db);
                return;
            }

            updateJob(jobProjectDatabasePath,
                      jobId,
                      100,
                      QStringLiteral("图片解析完成，结果已自动生效"),
                      analysisProgressContext(3, 3, QStringLiteral("完成图片解析"), 1, 1, QStringLiteral("张")));
            finishSuccess(QStringLiteral("图片解析完成，结果已自动生效"));
            return;
        }

        if (isSupportedTextAsset(asset.assetType, asset.extension)) {
            if (!updateAssetState(db, job.videoKey, VideoAnalysisStatus::Running, ConfirmationStatus::Pending, QString(), &errorMessage)) {
                finishFailure(errorMessage, &db, false);
                return;
            }

            updateRunning(15,
                          QStringLiteral("正在提取文本/文档内容"),
                          analysisProgressContext(1, 3, QStringLiteral("提取文本内容"), 0, 1, QStringLiteral("个文件")));
            bool textTruncated = false;
            QString extractionError;
            const auto sourceText = DocumentPreviewService::extractTextForSummary(asset.absolutePath, &textTruncated, &extractionError);
            if (sourceText.trimmed().isEmpty()) {
                const auto message = extractionError.trimmed().isEmpty()
                    ? QStringLiteral("当前文本/文档没有可解析内容。")
                    : extractionError;
                updateAssetState(db, job.videoKey, VideoAnalysisStatus::Failed, ConfirmationStatus::Pending, message, nullptr);
                finishFailure(message, &db, false);
                return;
            }

            if (!db.transaction()) {
                finishFailure(db.lastError().text(), &db);
                return;
            }
            if (!persistSourceTextForSearch(db, asset, sourceText, m_globalDatabaseManager->hasFts5(), &errorMessage)) {
                db.rollback();
                finishFailure(errorMessage, &db, false);
                return;
            }
            if (!db.commit()) {
                finishFailure(db.lastError().text(), &db);
                return;
            }

            updateRunning(45,
                          textTruncated ? QStringLiteral("正在归纳文本内容（已截取前段内容）") : QStringLiteral("正在归纳文本内容"),
                          analysisProgressContext(2, 3, QStringLiteral("归纳文本内容"), 1, 1, QStringLiteral("个文件")));
            int httpStatusCode = 0;
            QString summaryError;
            if (cancelled()) return;
            const auto summary = m_visionApiClient->summarizeText(asset.fileName,
                                                                  sourceText,
                                                                  config.baseUrl,
                                                                  config.apiKey,
                                                                  config.model,
                                                                  config.timeoutSec,
                                                                  &summaryError,
                                                                  &httpStatusCode);
            if (!summary.has_value()) {
                finishFailure(summaryError, &db);
                return;
            }

            updateRunning(85,
                          QStringLiteral("正在保存文本/文档解析结果"),
                          analysisProgressContext(3, 3, QStringLiteral("保存解析结果"), 1, 1, QStringLiteral("个文件")));
            if (!db.transaction()) {
                finishFailure(db.lastError().text(), &db);
                return;
            }
            if (!persistSummary(db, asset, *summary, {}, sourceText, m_globalDatabaseManager->hasFts5(), &errorMessage)) {
                db.rollback();
                finishFailure(errorMessage, &db, false);
                return;
            }
            if (!db.commit()) {
                finishFailure(db.lastError().text(), &db);
                return;
            }

            updateJob(jobProjectDatabasePath,
                      jobId,
                      100,
                      QStringLiteral("文本/文档解析完成，结果已自动生效"),
                      analysisProgressContext(3, 3, QStringLiteral("完成文本解析"), 1, 1, QStringLiteral("个文件")));
            finishSuccess(QStringLiteral("文本/文档解析完成，结果已自动生效"));
            return;
        }

        VideoAnalysisTask task;
        if (!loadAnalysisTask(db, job.videoKey, &task, &errorMessage)) {
            finishFailure(errorMessage, &db, false);
            return;
        }
        task.videoKey = job.videoKey;

        auto saveTask = [&](VideoAnalysisTaskStage stage, const QString &taskError = QString()) {
            task.stage = stage;
            task.lastErrorMessage = taskError;
            task.lastUpdatedAt = nowIso();
            return persistAnalysisTask(db, task, &errorMessage);
        };

        auto reloadFrames = [&]() {
            errorMessage.clear();
            return loadFrameRows(db, job.videoKey, &errorMessage);
        };

        auto buildContactSheet = [&](const QVector<FrameAnalysisRecord> &frames,
                                     int sourceFrameCount = 0) {
            Q_UNUSED(sourceFrameCount)

            QStringList imagePaths;
            imagePaths.reserve(frames.size());
            for (const auto &frame : frames) {
                if (!frame.imagePath.trimmed().isEmpty()) {
                    imagePaths.append(frame.imagePath);
                }
            }
            if (!imagePaths.isEmpty()) {
                ContactSheetBuilder::build(imagePaths,
                                           config.contactSheetFrameCount,
                                           Paths::projectContactSheetPath(asset.projectDatabasePath, job.videoKey, config.contactSheetFrameCount));
            }
        };

        auto persistSummaryAndTask = [&](const QVector<FrameAnalysisRecord> &frames, int summaryAttempts, const QString &successMessage) {
            QString summaryError;
            int summaryHttpStatus = 0;
            task.stage = VideoAnalysisTaskStage::Summarizing;
            task.summaryRetryCount = 0;
            if (!saveTask(VideoAnalysisTaskStage::Summarizing)) {
                finishFailure(errorMessage, &db, false);
                return false;
            }

            updateRunning(90,
                          QStringLiteral("正在汇总视频内容"),
                          analysisProgressContext(job.mode == AnalysisRunMode::SingleFrame ? 2 : 3,
                                                  job.mode == AnalysisRunMode::SingleFrame ? 2 : 4,
                                                  QStringLiteral("汇总视频内容"),
                                                  task.successfulFrames,
                                                  task.totalFrames,
                                                  QStringLiteral("帧")));
            if (cancelled()) return false;
            const auto summary = m_visionApiClient->summarizeVideo(asset.fileName,
                                                                   successfulFrames(frames),
                                                                   config.baseUrl,
                                                                   config.apiKey,
                                                                   config.model,
                                                                   config.timeoutSec,
                                                                   &summaryError,
                                                                   &summaryAttempts,
                                                                   &summaryHttpStatus);
            task.summaryRetryCount = qMax(0, summaryAttempts - 1);
            if (!summary.has_value()) {
                task.lastErrorMessage = summaryError;
                task.lastUpdatedAt = nowIso();
                persistAnalysisTask(db, task, nullptr);
                finishFailure(summaryError, &db);
                return false;
            }

            if (!db.transaction()) {
                finishFailure(db.lastError().text(), &db);
                return false;
            }
            if (stagingFreshRun) {
                if (!deleteAnalysisArtifacts(db,
                                             job.videoKey,
                                             m_globalDatabaseManager->hasFts5(),
                                             &errorMessage)) {
                    db.rollback();
                    finishFailure(errorMessage, &db);
                    return false;
                }
                for (const auto &frame : frames) {
                    const ExtractedFrame extractedFrame{
                        frame.frameNumber,
                        frame.timestampMs,
                        frame.imagePath
                    };
                    if (!insertFrameRow(db, job.videoKey, extractedFrame, &errorMessage)
                        || !updateFrameAnalysis(db, job.videoKey, frame, &errorMessage)) {
                        db.rollback();
                        finishFailure(errorMessage, &db);
                        return false;
                    }
                }
                if (!persistVisualAnalysisPlan(db, stagedPlan, &errorMessage)) {
                    db.rollback();
                    finishFailure(errorMessage, &db);
                    return false;
                }
            }
            if (!persistSummary(db, asset, *summary, frames, asset.sourceText, m_globalDatabaseManager->hasFts5(), &errorMessage)) {
                db.rollback();
                finishFailure(errorMessage, &db, false);
                return false;
            }
            task.stage = VideoAnalysisTaskStage::Completed;
            task.lastErrorMessage.clear();
            task.lastUpdatedAt = nowIso();
            if (!persistAnalysisTask(db, task, &errorMessage)) {
                db.rollback();
                finishFailure(errorMessage, &db, false);
                return false;
            }
            if (!db.commit()) {
                db.rollback();
                finishFailure(db.lastError().text(), &db);
                return false;
            }
            if (stagingFreshRun) {
                stagingFreshRun = false;
                preserveActiveAnalysisOnFailure = false;
                buildContactSheet(frames);
            }

            updateJob(jobProjectDatabasePath,
                      jobId,
                      100,
                      successMessage,
                      analysisProgressContext(job.mode == AnalysisRunMode::SingleFrame ? 2 : 4,
                                              job.mode == AnalysisRunMode::SingleFrame ? 2 : 4,
                                              job.mode == AnalysisRunMode::SingleFrame ? QStringLiteral("完成帧重解析") : QStringLiteral("完成视频解析"),
                                              task.completedFrames,
                                              task.totalFrames,
                                              QStringLiteral("帧")));
            completeJob(jobProjectDatabasePath, jobId, successMessage);
            reportAnalysisProgress(job.videoKey, 100, successMessage, JobState::Completed);
            return true;
        };

        if (!updateAssetState(db, job.videoKey, VideoAnalysisStatus::Running, ConfirmationStatus::Pending, QString(), &errorMessage)) {
            finishFailure(errorMessage, &db, false);
            return;
        }

        QVector<FrameAnalysisRecord> frames = reloadFrames();
        if (!errorMessage.trimmed().isEmpty()) {
            finishFailure(errorMessage, &db, false);
            return;
        }

        const auto cacheDirectory = Paths::projectFrameCacheDirectory(asset.projectDatabasePath, job.videoKey);

        if (job.mode == AnalysisRunMode::SingleFrame) {
            auto frameIndex = -1;
            for (int index = 0; index < frames.size(); ++index) {
                if (frames.at(index).frameNumber == job.frameNumber) {
                    frameIndex = index;
                    break;
                }
            }
            if (frameIndex < 0) {
                finishFailure(QStringLiteral("当前帧不存在，无法重解析。"), &db, false);
                return;
            }

            auto target = frames.at(frameIndex);
            const auto previousStatus = asset.analysisStatus;
            updateRunning(40,
                          QStringLiteral("正在重解析第 %1 帧").arg(target.frameNumber),
                          analysisProgressContext(1, 2, QStringLiteral("重解析视频帧"), 1, 1, QStringLiteral("帧"), target.frameNumber));
            int attempt = 0;
            while (attempt < kMaxFrameRetryCount) {
                ++attempt;
                int httpStatusCode = 0;
                QString frameError;
                const auto analysis = m_visionApiClient->analyzeFrame(target.imagePath,
                                                                      asset.fileName,
                                                                      config.baseUrl,
                                                                      config.apiKey,
                                                                      config.model,
                                                                      config.timeoutSec,
                                                                      &frameError,
                                                                      &httpStatusCode);
                target.retryCount = attempt;
                target.lastHttpStatus = httpStatusCode;
                target.lastAttemptAt = nowIso();
                if (analysis.has_value()) {
                    applyFrameAnalysis(&target, *analysis, config.model);
                    target.errorMessage.clear();
                    target.analysisState = FrameAnalysisState::Success;
                    break;
                }

                target.errorMessage = frameError;
                target.analysisState = attempt >= kMaxFrameRetryCount
                    ? FrameAnalysisState::Skipped
                    : FrameAnalysisState::Failed;
                if (!updateFrameAnalysis(db, job.videoKey, target, &errorMessage)) {
                    finishFailure(errorMessage, &db, false);
                    return;
                }
            }

            if (!updateFrameAnalysis(db, job.videoKey, target, &errorMessage)) {
                finishFailure(errorMessage, &db, false);
                return;
            }
            frames[frameIndex] = target;
            recalculateTaskCounts(frames, &task);
            task.stage = target.analysisState == FrameAnalysisState::Success
                ? VideoAnalysisTaskStage::Summarizing
                : VideoAnalysisTaskStage::AnalyzingFrames;
            task.lastErrorMessage = target.analysisState == FrameAnalysisState::Success ? QString() : target.errorMessage;
            task.lastUpdatedAt = nowIso();
            if (!persistAnalysisTask(db, task, &errorMessage)) {
                finishFailure(errorMessage, &db, false);
                return;
            }

            if (target.analysisState != FrameAnalysisState::Success) {
                if (previousStatus != VideoAnalysisStatus::Ready) {
                    updateAssetState(db, job.videoKey, VideoAnalysisStatus::Failed, ConfirmationStatus::Pending, target.errorMessage, nullptr);
                }
                finishFailure(target.errorMessage.trimmed().isEmpty() ? QStringLiteral("该帧重解析失败。") : target.errorMessage,
                              &db,
                              previousStatus != VideoAnalysisStatus::Ready);
                return;
            }

            buildContactSheet(frames);
            const auto succeededFrames = successfulFrames(frames);
            if (succeededFrames.isEmpty()) {
                finishFailure(QStringLiteral("没有可用于汇总的视频帧描述"), &db, false);
                return;
            }

            QString successMessage = QStringLiteral("第 %1 帧重解析完成。").arg(job.frameNumber);
            if (task.skippedFrames > 0) {
                successMessage += QStringLiteral(" ") + skippedFramesWarning(task.skippedFrames);
            }
            if (!persistSummaryAndTask(frames, 0, successMessage)) {
                return;
            }
        } else {
            VisualAnalysisPlan plan;
            bool hasPlan = false;
            if (!loadVisualAnalysisPlan(db, job.videoKey, &plan, &hasPlan, &errorMessage)) {
                finishFailure(errorMessage, &db, false);
                return;
            }
            const auto configuredInterval = qRound((config.extractionStrategy == VideoFrameExtractionStrategy::HighFidelity
                                                        ? qBound(0.1, config.frameIntervalSeconds, 0.25)
                                                        : qBound(0.1, config.frameIntervalSeconds, 240.0))
                                                   * 1000.0);
            const auto assetFingerprintChanged = hasPlan
                && (plan.assetSizeBytes != asset.sizeBytes || plan.assetModifiedAt != asset.modifiedAt);
            bool needsFreshExtraction = job.mode == AnalysisRunMode::Initial
                || job.mode == AnalysisRunMode::Rebuild
                || assetFingerprintChanged
                || !hasPlan
                || plan.samplingPolicy != samplingPolicy(config)
                || plan.frameInterval != configuredInterval
                || plan.structuredProfileVersion < cinevault::searchconfig::kStructuredVisionProfileVersion;
            if (needsFreshExtraction) {
                stagingFreshRun = hasPlan
                    || asset.analysisStatus == VideoAnalysisStatus::Ready
                    || !asset.summary.trimmed().isEmpty();
                preserveActiveAnalysisOnFailure = stagingFreshRun;
                if (stagingFreshRun) {
                    if (!updateAssetState(db,
                                          job.videoKey,
                                          previousAnalysisStatus,
                                          previousConfirmationStatus,
                                          previousAnalysisError,
                                          &errorMessage)) {
                        finishFailure(errorMessage, &db, false);
                        return;
                    }
                    stagingCacheDirectory = QDir(cacheDirectory).filePath(
                        QStringLiteral("runs/pending-%1")
                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
                }

                task = {};
                task.videoKey = job.videoKey;
                if (!saveTask(VideoAnalysisTaskStage::ExtractingFrames)) {
                    finishFailure(errorMessage, &db, false);
                    return;
                }

                updateRunning(8,
                              QStringLiteral("正在抽取视频帧：%1").arg(asset.fileName),
                              analysisProgressContext(1, 4, QStringLiteral("抽取视频帧")));

                FrameExtractionRequest request;
                request.assetId = asset.assetId;
                request.sourcePath = asset.absolutePath;
                request.outputDirectory = stagingFreshRun ? stagingCacheDirectory : cacheDirectory;
                request.strategy = config.extractionStrategy;
                request.intervalSeconds = config.frameIntervalSeconds;
                request.sceneThreshold = config.sceneThreshold;
                request.minimumSharpness = config.minimumSharpness;
                request.maxWidth = kAnalysisFrameMaxWidth;
                request.maxHeight = kAnalysisFrameMaxHeight;
                request.stopToken = stopToken;

                if (cancelled()) return;
                const auto extraction = m_ffmpegAdapter->extractFrames(request);
                if (!extraction.success || extraction.frames.isEmpty()) {
                    task.lastErrorMessage = extraction.errorMessage;
                    task.lastUpdatedAt = nowIso();
                    persistAnalysisTask(db, task, nullptr);
                    finishFailure(extraction.errorMessage, &db);
                    return;
                }

                plan = {};
                plan.videoKey = job.videoKey;
                plan.samplingPolicy = samplingPolicy(config);
                plan.frameInterval = extraction.frameInterval;
                plan.structuredProfileVersion = cinevault::searchconfig::kStructuredVisionProfileVersion;
                plan.sourceFrameCount = extraction.sourceFrameCount;
                plan.plannedFrameCount = extraction.frames.size();
                plan.assetSizeBytes = asset.sizeBytes;
                plan.assetModifiedAt = asset.modifiedAt;
                plan.createdAt = nowIso();
                if (stagingFreshRun) {
                    stagedPlan = plan;
                    frames.clear();
                    frames.reserve(extraction.frames.size());
                    for (const auto &extracted : extraction.frames) {
                        FrameAnalysisRecord frame;
                        frame.videoKey = job.videoKey;
                        frame.frameNumber = extracted.frameNumber;
                        frame.timestampMs = extracted.timestampMs;
                        frame.imagePath = extracted.imagePath;
                        frame.analysisState = FrameAnalysisState::Pending;
                        frames.append(frame);
                    }
                } else {
                    if (!db.transaction()) {
                        finishFailure(db.lastError().text(), &db);
                        return;
                    }
                    if (!deleteAnalysisArtifacts(db,
                                                 job.videoKey,
                                                 m_globalDatabaseManager->hasFts5(),
                                                 &errorMessage)
                        || !syncFramePlanRows(db, job.videoKey, extraction.frames, false, &errorMessage)
                        || !persistVisualAnalysisPlan(db, plan, &errorMessage)) {
                        db.rollback();
                        finishFailure(errorMessage, &db, false);
                        return;
                    }
                    if (!db.commit()) {
                        finishFailure(db.lastError().text(), &db);
                        return;
                    }
                    frames = reloadFrames();
                    if (!errorMessage.trimmed().isEmpty()) {
                        finishFailure(errorMessage, &db, false);
                        return;
                    }
                }
                recalculateTaskCounts(frames, &task);
                if (!saveTask(VideoAnalysisTaskStage::AnalyzingFrames)) {
                    finishFailure(errorMessage, &db, false);
                    return;
                }
                if (!stagingFreshRun) {
                    buildContactSheet(frames);
                }
                updateRunning(10,
                              QStringLiteral("已抽取 %1 帧，开始视觉解析").arg(frames.size()),
                              analysisProgressContext(2, 4, QStringLiteral("解析视频帧"), 0, frames.size(), QStringLiteral("帧")));
            } else {
                recalculateTaskCounts(frames, &task);
                if (task.stage == VideoAnalysisTaskStage::Pending || task.stage == VideoAnalysisTaskStage::ExtractingFrames) {
                    if (!saveTask(VideoAnalysisTaskStage::AnalyzingFrames)) {
                        finishFailure(errorMessage, &db, false);
                        return;
                    }
                }
                buildContactSheet(frames);
                updateRunning(10,
                              QStringLiteral("继续解析视频帧，已完成 %1/%2，跳过 %3 帧")
                                  .arg(task.completedFrames)
                                  .arg(task.totalFrames)
                                  .arg(task.skippedFrames),
                              analysisProgressContext(2,
                                                      4,
                                                      QStringLiteral("解析视频帧"),
                                                      task.completedFrames,
                                                      task.totalFrames,
                                                      QStringLiteral("帧"),
                                                      0,
                                                      QStringLiteral("跳过 %1 帧").arg(task.skippedFrames)));
            }

            const auto maxFrameRequests = VisionApiClient::maxConcurrentFrameRequests(config.model);
            if (maxFrameRequests > 1) {
                QThreadPool requestPool;
                requestPool.setMaxThreadCount(maxFrameRequests);

                while (true) {
                    if (cancelled()) return;
                    QVector<int> frameIndexes;
                    frameIndexes.reserve(maxFrameRequests);
                    for (int index = 0; index < frames.size() && frameIndexes.size() < maxFrameRequests; ++index) {
                        if (!VisualAnalysisMetadata::isFrameAnalysisComplete(
                                frames.at(index),
                                cinevault::searchconfig::kStructuredVisionProfileVersion)) {
                            frameIndexes.append(index);
                        }
                    }
                    if (frameIndexes.isEmpty()) {
                        break;
                    }

                    QVector<QFuture<FrameRequestResult>> pendingRequests;
                    pendingRequests.reserve(frameIndexes.size());
                    for (const auto frameIndex : frameIndexes) {
                        const auto sourceFrame = frames.at(frameIndex);
                        const auto initialAttempt = job.mode == AnalysisRunMode::Resume
                            ? 0
                            : qBound(0, sourceFrame.retryCount, kMaxFrameRetryCount);
                        pendingRequests.append(QtConcurrent::run(&requestPool,
                                                                 [this, frameIndex, sourceFrame, initialAttempt, asset, config, stopToken]() {
                            FrameRequestResult result;
                            result.frameIndex = frameIndex;
                            result.attempt = initialAttempt;
                            while (result.attempt < kMaxFrameRetryCount
                                   && !stopToken.stop_requested()
                                   && !m_shuttingDown.load()) {
                                ++result.attempt;
                                result.wasAttempted = true;
                                result.analysis = m_visionApiClient->analyzeFrame(sourceFrame.imagePath,
                                                                                   asset.fileName,
                                                                                   config.baseUrl,
                                                                                   config.apiKey,
                                                                                   config.model,
                                                                                   config.timeoutSec,
                                                                                   &result.errorMessage,
                                                                                   &result.httpStatusCode);
                                if (result.analysis.has_value()) {
                                    break;
                                }
                            }
                            return result;
                        }));
                    }

                    for (auto &pendingRequest : pendingRequests) {
                        const auto result = pendingRequest.result();
                        if (cancelled()) return;
                        auto &frame = frames[result.frameIndex];
                        if (result.wasAttempted) {
                            frame.retryCount = result.attempt;
                            frame.lastHttpStatus = result.httpStatusCode;
                            frame.lastAttemptAt = nowIso();
                            if (result.analysis.has_value()) {
                                applyFrameAnalysis(&frame, *result.analysis, config.model);
                                frame.errorMessage.clear();
                                frame.analysisState = FrameAnalysisState::Success;
                            } else {
                                frame.errorMessage = result.errorMessage;
                                frame.analysisState = result.attempt >= kMaxFrameRetryCount
                                    ? FrameAnalysisState::Skipped
                                    : FrameAnalysisState::Failed;
                            }
                        }
                        if (!stagingFreshRun
                            && !updateFrameAnalysis(db, job.videoKey, frame, &errorMessage)) {
                            finishFailure(errorMessage, &db, false);
                            return;
                        }
                        recalculateTaskCounts(frames, &task);
                        if (!saveTask(VideoAnalysisTaskStage::AnalyzingFrames,
                                      frame.analysisState == FrameAnalysisState::Skipped ? frame.errorMessage : QString())) {
                            finishFailure(errorMessage, &db, false);
                            return;
                        }

                        const auto progress = task.totalFrames > 0
                            ? 10 + ((static_cast<qint64>(task.completedFrames) * 75) / task.totalFrames)
                            : 10;
                        updateRunning(progress,
                                      QStringLiteral("正在解析视频帧（MiniMax-M3，最多 %1 并发），已完成 %2/%3，成功 %4 帧，跳过 %5 帧")
                                          .arg(maxFrameRequests)
                                          .arg(task.completedFrames)
                                          .arg(task.totalFrames)
                                          .arg(task.successfulFrames)
                                          .arg(task.skippedFrames),
                                      analysisProgressContext(2,
                                                              4,
                                                              QStringLiteral("解析视频帧"),
                                                              task.completedFrames,
                                                              task.totalFrames,
                                                              QStringLiteral("帧"),
                                                              frame.frameNumber,
                                                              QStringLiteral("成功 %1 · 跳过 %2")
                                                                  .arg(task.successfulFrames)
                                                                  .arg(task.skippedFrames)));
                    }
                }
            } else {
            for (int index = 0; index < frames.size(); ++index) {
                if (cancelled()) return;
                auto &frame = frames[index];
                if (VisualAnalysisMetadata::isFrameAnalysisComplete(
                        frame,
                        cinevault::searchconfig::kStructuredVisionProfileVersion)) {
                    continue;
                }

                int attempt = job.mode == AnalysisRunMode::Resume
                    ? 0
                    : qBound(0, frame.retryCount, kMaxFrameRetryCount);
                while (attempt < kMaxFrameRetryCount
                       && !stopToken.stop_requested()
                       && !m_shuttingDown.load()) {
                    ++attempt;
                    int httpStatusCode = 0;
                    QString frameError;
                    const auto analysis = m_visionApiClient->analyzeFrame(frame.imagePath,
                                                                          asset.fileName,
                                                                          config.baseUrl,
                                                                          config.apiKey,
                                                                          config.model,
                                                                          config.timeoutSec,
                                                                          &frameError,
                                                                          &httpStatusCode);
                    frame.retryCount = attempt;
                    frame.lastHttpStatus = httpStatusCode;
                    frame.lastAttemptAt = nowIso();

                    if (analysis.has_value()) {
                        applyFrameAnalysis(&frame, *analysis, config.model);
                        frame.errorMessage.clear();
                        frame.analysisState = FrameAnalysisState::Success;
                        break;
                    }

                    frame.errorMessage = frameError;
                    frame.analysisState = attempt >= kMaxFrameRetryCount
                        ? FrameAnalysisState::Skipped
                        : FrameAnalysisState::Failed;
                    if (!stagingFreshRun
                        && !updateFrameAnalysis(db, job.videoKey, frame, &errorMessage)) {
                        finishFailure(errorMessage, &db, false);
                        return;
                    }
                }

                if (!stagingFreshRun
                    && !updateFrameAnalysis(db, job.videoKey, frame, &errorMessage)) {
                    finishFailure(errorMessage, &db, false);
                    return;
                }

                recalculateTaskCounts(frames, &task);
                if (!saveTask(VideoAnalysisTaskStage::AnalyzingFrames,
                              frame.analysisState == FrameAnalysisState::Skipped ? frame.errorMessage : QString())) {
                    finishFailure(errorMessage, &db, false);
                    return;
                }

                const auto progress = task.totalFrames > 0
                    ? 10 + ((static_cast<qint64>(task.completedFrames) * 75) / task.totalFrames)
                    : 10;
                updateRunning(progress,
                              QStringLiteral("正在解析视频帧，已完成 %1/%2，成功 %3 帧，跳过 %4 帧")
                                  .arg(task.completedFrames)
                                  .arg(task.totalFrames)
                                  .arg(task.successfulFrames)
                                  .arg(task.skippedFrames),
                              analysisProgressContext(2,
                                                      4,
                                                      QStringLiteral("解析视频帧"),
                                                      task.completedFrames,
                                                      task.totalFrames,
                                                      QStringLiteral("帧"),
                                                      frame.frameNumber,
                                                      QStringLiteral("成功 %1 · 跳过 %2").arg(task.successfulFrames).arg(task.skippedFrames)));
            }
            }

            const auto succeededFrames = successfulFrames(frames);
            if (succeededFrames.isEmpty()) {
                const auto failMessage = QStringLiteral("所有视频帧都解析失败");
                task.lastErrorMessage = failMessage;
                task.lastUpdatedAt = nowIso();
                persistAnalysisTask(db, task, nullptr);
                finishFailure(failMessage, &db);
                return;
            }

            QString successMessage = task.skippedFrames > 0
                ? QStringLiteral("视频解析完成，成功 %1 帧，跳过 %2 帧，结果已自动生效").arg(task.successfulFrames).arg(task.skippedFrames)
                : QStringLiteral("视频解析完成，结果已自动生效");
            const auto remainingStructuredGaps = std::count_if(
                frames.cbegin(), frames.cend(), [](const FrameAnalysisRecord &frame) {
                    return !VisualAnalysisMetadata::isFrameAnalysisComplete(
                        frame,
                        cinevault::searchconfig::kStructuredVisionProfileVersion);
                });
            if (remainingStructuredGaps > 0) {
                successMessage += QStringLiteral("；仍有 %1 帧结构化事实待补齐").arg(remainingStructuredGaps);
            }
            if (!persistSummaryAndTask(frames, 0, successMessage)) {
                return;
            }
        }

        notifyCatalogChanged(job.videoKey);
        QMetaObject::invokeMethod(this, [this, videoKey = job.videoKey]() {
            finishCurrentAnalysis(videoKey, true);
        }, Qt::QueuedConnection);
    });
    m_futures.addFuture(future);
}

void VideoAnalysisService::finishCurrentAnalysis(const QString &videoKey, bool succeeded)
{
    if (m_currentVideoKey != videoKey) {
        return;
    }
    const auto completedJob = m_currentJob;
    m_currentJob = {};
    m_currentVideoKey.clear();
    m_analysisRunning = false;
    m_batchCurrentProgress = 0;
    m_batchCurrentDetail.clear();
    emit analysisQueueChanged(m_currentVideoKey, m_analysisQueue.size());
    notifyBatchChanged();
    if (succeeded && completedJob.mode != AnalysisRunMode::SingleFrame) {
        QString ignoredError;
        enqueueConfiguredDimensionsIfNeeded(videoKey, &ignoredError);
    }
    startNextAnalysis();
}

void VideoAnalysisService::reportAnalysisProgress(const QString &videoKey,
                                                  qint64 progress,
                                                  const QString &detail,
                                                  JobState state,
                                                  const QString &errorMessage)
{
    const auto normalizedProgress = qBound<qint64>(0LL, progress, 100LL);
    ensureBatchItem(videoKey);
    switch (state) {
    case JobState::Pending:
        setBatchItemState(videoKey, BatchItemState::Queued);
        break;
    case JobState::Running:
        setBatchItemState(videoKey, BatchItemState::Running);
        m_batchCurrentProgress = normalizedProgress;
        m_batchCurrentDetail = detail;
        break;
    case JobState::Completed:
        setBatchItemState(videoKey, BatchItemState::Completed);
        if (m_currentVideoKey == videoKey) {
            m_batchCurrentProgress = 100;
            m_batchCurrentDetail = detail;
        }
        break;
    case JobState::Failed:
        setBatchItemState(videoKey, BatchItemState::Failed);
        if (m_currentVideoKey == videoKey) {
            m_batchCurrentProgress = 100;
            m_batchCurrentDetail = errorMessage.trimmed().isEmpty() ? detail : errorMessage;
        }
        break;
    case JobState::Cancelled:
        break;
    }
    notifyBatchChanged();

    if (QThread::currentThread() == thread()) {
        emit analysisProgressChanged(videoKey, normalizedProgress, detail, static_cast<int>(state), errorMessage);
        return;
    }

    QMetaObject::invokeMethod(this, [this, videoKey, normalizedProgress, detail, state, errorMessage]() {
        emit analysisProgressChanged(videoKey, normalizedProgress, detail, static_cast<int>(state), errorMessage);
    }, Qt::QueuedConnection);
}

void VideoAnalysisService::resetBatchSummaryIfIdle()
{
    if (m_analysisRunning
        || !m_currentVideoKey.trimmed().isEmpty()
        || !m_analysisQueue.isEmpty()
        || !m_dimensionAnalysisQueue.isEmpty()) {
        return;
    }
    if (m_batchStates.isEmpty()) {
        return;
    }
    m_batchStates.clear();
    m_batchLabels.clear();
    m_batchCurrentProgress = 0;
    m_batchCurrentDetail.clear();
}

void VideoAnalysisService::ensureBatchItem(const QString &videoKey)
{
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        return;
    }
    if (!m_batchStates.contains(normalizedKey)) {
        m_batchStates.insert(normalizedKey, BatchItemState::Queued);
    }
    if (!m_batchLabels.contains(normalizedKey)) {
        m_batchLabels.insert(normalizedKey, lookupVideoLabel(normalizedKey));
    }
}

void VideoAnalysisService::setBatchItemState(const QString &videoKey, BatchItemState state)
{
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty()) {
        return;
    }
    m_batchStates.insert(normalizedKey, state);
}

void VideoAnalysisService::notifyBatchChanged()
{
    if (QThread::currentThread() == thread()) {
        emit analysisBatchChanged();
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        emit analysisBatchChanged();
    }, Qt::QueuedConnection);
}

QString VideoAnalysisService::lookupVideoLabel(const QString &videoKey) const
{
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        return videoKey;
    }

    QSqlQuery query(m_globalDatabaseManager->database());
    query.prepare(QStringLiteral("SELECT file_name FROM global_video_asset WHERE video_key = ? LIMIT 1"));
    query.addBindValue(videoKey);
    if (!query.exec() || !query.next()) {
        return videoKey;
    }

    const auto label = query.value(0).toString().trimmed();
    return label.isEmpty() ? videoKey : label;
}

void VideoAnalysisService::updateJob(const QString &projectDatabasePath,
                                     qint64 jobId,
                                     qint64 progress,
                                     const QString &detail,
                                     const JobProgressContext &progressContext)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, progress, detail, progressContext]() {
        engine->updateJobForProject(projectDatabasePath, jobId, progress, detail, progressContext);
    }, Qt::QueuedConnection);
}

void VideoAnalysisService::updateJobSubject(const QString &projectDatabasePath, qint64 jobId, const JobSubject &subject)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, subject]() {
        engine->updateJobSubjectForProject(projectDatabasePath, jobId, subject);
    }, Qt::QueuedConnection);
}

void VideoAnalysisService::completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, detail]() {
        engine->completeJobForProject(projectDatabasePath, jobId, detail);
    }, Qt::QueuedConnection);
}

void VideoAnalysisService::failJob(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, errorMessage]() {
        engine->failJobForProject(projectDatabasePath, jobId, errorMessage);
    }, Qt::QueuedConnection);
}

void VideoAnalysisService::notifyCatalogChanged(const QString &videoKey)
{
    QMetaObject::invokeMethod(this, [this, videoKey]() {
        emit catalogChanged();
        emit searchDocumentChanged(videoKey);
    }, Qt::QueuedConnection);
}
