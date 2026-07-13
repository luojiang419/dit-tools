#include "application/MaterialCenterQueryService.h"

#include "core/search/SearchEngine.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Formatters.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool execOrEmpty(QSqlQuery &query)
{
    return query.exec();
}

QStringList parseJsonList(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return {};
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return {};
    }

    QStringList items;
    const auto array = document.array();
    for (const auto &value : array) {
        const auto item = value.toString().trimmed();
        if (!item.isEmpty()) {
            items.append(item);
        }
    }
    return items;
}

GlobalVideoAsset readAssetRow(const QSqlQuery &query)
{
    GlobalVideoAsset asset;
    asset.videoKey = query.value(0).toString();
    asset.assetKey = asset.videoKey;
    asset.projectUuid = query.value(1).toString();
    asset.projectName = query.value(2).toString();
    asset.projectDatabasePath = query.value(3).toString();
    asset.sourceRootId = query.value(4).toLongLong();
    asset.sourceRootName = query.value(5).toString();
    asset.assetId = query.value(6).toLongLong();
    asset.fileName = query.value(7).toString();
    asset.extension = query.value(8).toString();
    asset.absolutePath = query.value(9).toString();
    asset.relativePath = query.value(10).toString();
    asset.assetType = static_cast<AssetType>(query.value(11).toInt());
    asset.sizeBytes = query.value(12).toLongLong();
    asset.modifiedAt = query.value(13).toString();
    asset.durationMs = query.value(14).toLongLong();
    asset.technicalSummary = query.value(15).toString();
    asset.sourceText = query.value(16).toString();
    asset.thumbnailPath = query.value(17).toString();
    asset.thumbnailStatus = static_cast<ThumbnailStatus>(query.value(18).toInt());
    asset.analysisStatus = static_cast<VideoAnalysisStatus>(query.value(19).toInt());
    asset.confirmationStatus = static_cast<ConfirmationStatus>(query.value(20).toInt());
    asset.errorMessage = query.value(21).toString();
    asset.updatedAt = query.value(22).toString();
    asset.summary = query.value(23).toString();
    asset.keywords = parseJsonList(query.value(24).toString());
    asset.scenes = parseJsonList(query.value(25).toString());
    asset.searchText = query.value(26).toString();
    asset.analyzedAt = query.value(27).toString();
    asset.confirmedAt = query.value(28).toString();
    asset.analysisTask.videoKey = asset.videoKey;
    asset.analysisTask.stage = static_cast<VideoAnalysisTaskStage>(query.value(29).toInt());
    asset.analysisTask.totalFrames = query.value(30).toInt();
    asset.analysisTask.completedFrames = query.value(31).toInt();
    asset.analysisTask.successfulFrames = query.value(32).toInt();
    asset.analysisTask.skippedFrames = query.value(33).toInt();
    asset.analysisTask.summaryRetryCount = query.value(34).toInt();
    asset.analysisTask.lastErrorMessage = query.value(35).toString();
    asset.analysisTask.lastUpdatedAt = query.value(36).toString();
    return asset;
}
}

MaterialCenterQueryService::MaterialCenterQueryService(GlobalDatabaseManager *globalDatabaseManager,
                                                       SearchEngine *searchEngine,
                                                       QObject *parent)
    : QObject(parent)
    , m_globalDatabaseManager(globalDatabaseManager)
    , m_searchEngine(searchEngine)
{
}

QVariantList MaterialCenterQueryService::fetchProjectOptions() const
{
    QVariantList options;
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        return options;
    }

    QSqlQuery query(m_globalDatabaseManager->database());
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT project_uuid, project_name FROM global_video_asset ORDER BY project_name COLLATE NOCASE"))) {
        return options;
    }

    while (query.next()) {
        options.append(QVariantMap{
            {QStringLiteral("value"), query.value(0).toString()},
            {QStringLiteral("label"), query.value(1).toString()}
        });
    }
    return options;
}

QVariantList MaterialCenterQueryService::fetchSourceOptions(const QString &projectUuid) const
{
    QVariantList options;
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        return options;
    }

    QString sql = QStringLiteral("SELECT DISTINCT source_root_name FROM global_video_asset");
    if (!projectUuid.trimmed().isEmpty()) {
        sql += QStringLiteral(" WHERE project_uuid = ?");
    }
    sql += QStringLiteral(" ORDER BY source_root_name COLLATE NOCASE");

    QSqlQuery query(m_globalDatabaseManager->database());
    query.prepare(sql);
    if (!projectUuid.trimmed().isEmpty()) {
        query.addBindValue(projectUuid.trimmed());
    }
    if (!execOrEmpty(query)) {
        return options;
    }

    while (query.next()) {
        const auto name = query.value(0).toString().trimmed();
        if (!name.isEmpty()) {
            options.append(QVariantMap{
                {QStringLiteral("value"), name},
                {QStringLiteral("label"), name}
            });
        }
    }
    return options;
}

QVariantList MaterialCenterQueryService::fetchAssetTypeOptions() const
{
    QVariantList options;
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        return options;
    }

    QSqlQuery query(m_globalDatabaseManager->database());
    if (!query.exec(QStringLiteral("SELECT DISTINCT asset_type FROM global_video_asset ORDER BY asset_type"))) {
        return options;
    }

    while (query.next()) {
        const auto type = static_cast<AssetType>(query.value(0).toInt());
        options.append(QVariantMap{
            {QStringLiteral("value"), static_cast<int>(type)},
            {QStringLiteral("label"), Formatters::assetTypeLabel(type)}
        });
    }
    return options;
}

QVector<GlobalVideoAsset> MaterialCenterQueryService::fetchAssets(const QString &keyword,
                                                                  const QString &projectUuid,
                                                                  const QString &sourceName,
                                                                  int analysisStatusFilter,
                                                                  int confirmationStatusFilter,
                                                                  int assetTypeFilter) const
{
    QVector<GlobalVideoAsset> assets;
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        return assets;
    }

    QString sql = QStringLiteral(
        "SELECT g.video_key, g.project_uuid, g.project_name, g.project_database_path, g.source_root_id, g.source_root_name, "
        "g.asset_id, g.file_name, COALESCE(g.extension, ''), g.absolute_path, g.relative_path, COALESCE(g.asset_type, 1), "
        "g.size_bytes, g.modified_at, g.duration_ms, COALESCE(g.technical_summary, ''), COALESCE(g.source_text, ''), "
        "COALESCE(g.thumbnail_path, ''), COALESCE(g.thumbnail_status, 0), g.analysis_status, g.confirmation_status, COALESCE(g.error_message, ''), "
        "g.updated_at, COALESCE(r.summary, ''), COALESCE(r.keywords_json, '[]'), COALESCE(r.scenes_json, '[]'), "
        "COALESCE(r.search_text, ''), COALESCE(r.analyzed_at, ''), COALESCE(r.confirmed_at, ''), "
        "COALESCE(t.stage, 0), COALESCE(t.total_frames, 0), COALESCE(t.completed_frames, 0), "
        "COALESCE(t.successful_frames, 0), COALESCE(t.skipped_frames, 0), COALESCE(t.summary_retry_count, 0), "
        "COALESCE(t.last_error_message, ''), COALESCE(t.last_updated_at, '') "
        "FROM global_video_asset g "
        "LEFT JOIN video_analysis_result r ON r.video_key = g.video_key "
        "LEFT JOIN video_analysis_task t ON t.video_key = g.video_key "
        "WHERE 1 = 1");

    QVariantList binds;
    if (!projectUuid.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.project_uuid = ?");
        binds.append(projectUuid.trimmed());
    }
    if (!sourceName.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND g.source_root_name = ?");
        binds.append(sourceName.trimmed());
    }
    if (analysisStatusFilter >= 0) {
        sql += QStringLiteral(" AND g.analysis_status = ?");
        binds.append(analysisStatusFilter);
    }
    if (confirmationStatusFilter >= 0) {
        sql += QStringLiteral(" AND g.confirmation_status = ?");
        binds.append(confirmationStatusFilter);
    }
    if (assetTypeFilter >= 0) {
        sql += QStringLiteral(" AND g.asset_type = ?");
        binds.append(assetTypeFilter);
    }
    if (!keyword.trimmed().isEmpty()) {
        const auto likePattern = m_searchEngine ? m_searchEngine->buildLikePattern(keyword) : QStringLiteral("%%");
        if (m_globalDatabaseManager->hasFts5() && m_searchEngine) {
            sql += QStringLiteral(
                " AND (g.video_key IN (SELECT video_key FROM video_search_fts WHERE video_search_fts MATCH ?) "
                "OR COALESCE(r.search_text, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(g.source_text, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(g.technical_summary, '') LIKE ? ESCAPE '\\' "
                "OR g.file_name LIKE ? ESCAPE '\\' "
                "OR g.absolute_path LIKE ? ESCAPE '\\' "
                "OR g.relative_path LIKE ? ESCAPE '\\' "
                "OR COALESCE(g.extension, '') LIKE ? ESCAPE '\\' "
                "OR g.project_name LIKE ? ESCAPE '\\' "
                "OR g.source_root_name LIKE ? ESCAPE '\\' "
                "OR EXISTS ("
                "SELECT 1 FROM material_dimension_analysis d WHERE d.video_key = g.video_key "
                "AND (COALESCE(d.dimension_name, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(d.detail, '') LIKE ? ESCAPE '\\')) "
                "OR EXISTS ("
                "SELECT 1 FROM video_frame_analysis f WHERE f.video_key = g.video_key "
                "AND (COALESCE(f.caption, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.tags_json, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.objects_json, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.actions, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.setting_text, '') LIKE ? ESCAPE '\\')))");
            binds.append(m_searchEngine->buildFtsQuery(keyword));
        } else {
            sql += QStringLiteral(
                " AND (COALESCE(r.search_text, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(g.source_text, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(g.technical_summary, '') LIKE ? ESCAPE '\\' "
                "OR g.file_name LIKE ? ESCAPE '\\' "
                "OR g.absolute_path LIKE ? ESCAPE '\\' "
                "OR g.relative_path LIKE ? ESCAPE '\\' "
                "OR COALESCE(g.extension, '') LIKE ? ESCAPE '\\' "
                "OR g.project_name LIKE ? ESCAPE '\\' "
                "OR g.source_root_name LIKE ? ESCAPE '\\' "
                "OR EXISTS ("
                "SELECT 1 FROM material_dimension_analysis d WHERE d.video_key = g.video_key "
                "AND (COALESCE(d.dimension_name, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(d.detail, '') LIKE ? ESCAPE '\\')) "
                "OR EXISTS ("
                "SELECT 1 FROM video_frame_analysis f WHERE f.video_key = g.video_key "
                "AND (COALESCE(f.caption, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.tags_json, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.objects_json, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.actions, '') LIKE ? ESCAPE '\\' "
                "OR COALESCE(f.setting_text, '') LIKE ? ESCAPE '\\')))");
        }
        for (int index = 0; index < 16; ++index) {
            binds.append(likePattern);
        }
    }
    sql += QStringLiteral(
        " ORDER BY g.file_name COLLATE NOCASE ASC, "
        "g.project_name COLLATE NOCASE ASC, "
        "g.relative_path COLLATE NOCASE ASC, "
        "g.video_key ASC LIMIT 2000");

    QSqlQuery query(m_globalDatabaseManager->database());
    query.prepare(sql);
    for (const auto &bind : binds) {
        query.addBindValue(bind);
    }
    if (!execOrEmpty(query)) {
        return assets;
    }

    while (query.next()) {
        assets.append(readAssetRow(query));
    }
    return assets;
}

VideoAnalysisDetail MaterialCenterQueryService::fetchDetail(const QString &videoKey) const
{
    VideoAnalysisDetail detail;
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen() || videoKey.trimmed().isEmpty()) {
        return detail;
    }

    QSqlQuery assetQuery(m_globalDatabaseManager->database());
    assetQuery.prepare(QStringLiteral(
        "SELECT g.video_key, g.project_uuid, g.project_name, g.project_database_path, g.source_root_id, g.source_root_name, "
        "g.asset_id, g.file_name, COALESCE(g.extension, ''), g.absolute_path, g.relative_path, COALESCE(g.asset_type, 1), "
        "g.size_bytes, g.modified_at, g.duration_ms, COALESCE(g.technical_summary, ''), COALESCE(g.source_text, ''), "
        "COALESCE(g.thumbnail_path, ''), COALESCE(g.thumbnail_status, 0), g.analysis_status, g.confirmation_status, COALESCE(g.error_message, ''), "
        "g.updated_at, COALESCE(r.summary, ''), COALESCE(r.keywords_json, '[]'), COALESCE(r.scenes_json, '[]'), "
        "COALESCE(r.search_text, ''), COALESCE(r.analyzed_at, ''), COALESCE(r.confirmed_at, ''), "
        "COALESCE(t.stage, 0), COALESCE(t.total_frames, 0), COALESCE(t.completed_frames, 0), "
        "COALESCE(t.successful_frames, 0), COALESCE(t.skipped_frames, 0), COALESCE(t.summary_retry_count, 0), "
        "COALESCE(t.last_error_message, ''), COALESCE(t.last_updated_at, '') "
        "FROM global_video_asset g "
        "LEFT JOIN video_analysis_result r ON r.video_key = g.video_key "
        "LEFT JOIN video_analysis_task t ON t.video_key = g.video_key "
        "WHERE g.video_key = ?"));
    assetQuery.addBindValue(videoKey.trimmed());
    if (!execOrEmpty(assetQuery) || !assetQuery.next()) {
        return detail;
    }
    detail.asset = readAssetRow(assetQuery);

    QSqlQuery frameQuery(m_globalDatabaseManager->database());
    frameQuery.prepare(QStringLiteral(
        "SELECT id, frame_number, timestamp_ms, COALESCE(image_path, ''), COALESCE(caption, ''), "
        "COALESCE(tags_json, '[]'), COALESCE(objects_json, '[]'), COALESCE(actions, ''), "
        "COALESCE(setting_text, ''), COALESCE(error_message, ''), COALESCE(analysis_state, 0), "
        "COALESCE(retry_count, 0), COALESCE(last_http_status, 0), COALESCE(last_attempt_at, '') "
        "FROM video_frame_analysis WHERE video_key = ? ORDER BY frame_number"));
    frameQuery.addBindValue(videoKey.trimmed());
    if (!execOrEmpty(frameQuery)) {
        return detail;
    }

    while (frameQuery.next()) {
        FrameAnalysisRecord frame;
        frame.id = frameQuery.value(0).toLongLong();
        frame.videoKey = videoKey.trimmed();
        frame.frameNumber = frameQuery.value(1).toInt();
        frame.timestampMs = frameQuery.value(2).toLongLong();
        frame.imagePath = frameQuery.value(3).toString();
        frame.caption = frameQuery.value(4).toString();
        frame.tags = parseJsonList(frameQuery.value(5).toString());
        frame.objects = parseJsonList(frameQuery.value(6).toString());
        frame.actions = frameQuery.value(7).toString();
        frame.setting = frameQuery.value(8).toString();
        frame.errorMessage = frameQuery.value(9).toString();
        frame.analysisState = static_cast<FrameAnalysisState>(frameQuery.value(10).toInt());
        frame.retryCount = frameQuery.value(11).toInt();
        frame.lastHttpStatus = frameQuery.value(12).toInt();
        frame.lastAttemptAt = frameQuery.value(13).toString();
        detail.frames.append(frame);
    }

    QSqlQuery dimensionQuery(m_globalDatabaseManager->database());
    dimensionQuery.prepare(QStringLiteral(
        "SELECT dimension_name, detail, analyzed_at "
        "FROM material_dimension_analysis WHERE video_key = ? "
        "ORDER BY analyzed_at DESC, id DESC"));
    dimensionQuery.addBindValue(videoKey.trimmed());
    if (!execOrEmpty(dimensionQuery)) {
        return detail;
    }

    while (dimensionQuery.next()) {
        MaterialDimensionAnalysis dimension;
        dimension.name = dimensionQuery.value(0).toString();
        dimension.detail = dimensionQuery.value(1).toString();
        dimension.analyzedAt = dimensionQuery.value(2).toString();
        detail.dimensionAnalyses.append(dimension);
    }
    return detail;
}
