#include "application/MaterialCenterQueryService.h"

#include "core/search/SearchReliabilityEvaluator.h"

#include "core/search/SearchEngine.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Formatters.h"
#include "shared/SearchConfiguration.h"
#include "shared/VisualAnalysisMetadata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <utility>

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
    asset.captureTime = query.value(37).toString();
    asset.captureDate = query.value(38).toString();
    asset.captureTimeSource = query.value(39).toString();
    asset.captureTimeConfidence = query.value(40).toDouble();
    return asset;
}

QString assetResultSelectClause()
{
    return QStringLiteral(
        "SELECT g.video_key, g.project_uuid, g.project_name, g.project_database_path, g.source_root_id, g.source_root_name, "
        "g.asset_id, g.file_name, COALESCE(g.extension, ''), g.absolute_path, g.relative_path, COALESCE(g.asset_type, 1), "
        "g.size_bytes, g.modified_at, g.duration_ms, COALESCE(g.technical_summary, ''), COALESCE(g.source_text, ''), "
        "COALESCE(g.thumbnail_path, ''), COALESCE(g.thumbnail_status, 0), g.analysis_status, g.confirmation_status, "
        "COALESCE(g.error_message, ''), g.updated_at, COALESCE(r.summary, ''), COALESCE(r.keywords_json, '[]'), "
        "COALESCE(r.scenes_json, '[]'), COALESCE(r.search_text, ''), COALESCE(r.analyzed_at, ''), "
        "COALESCE(r.confirmed_at, ''), COALESCE(t.stage, 0), COALESCE(t.total_frames, 0), "
        "COALESCE(t.completed_frames, 0), COALESCE(t.successful_frames, 0), COALESCE(t.skipped_frames, 0), "
        "COALESCE(t.summary_retry_count, 0), COALESCE(t.last_error_message, ''), COALESCE(t.last_updated_at, ''), "
        "COALESCE(g.capture_time, ''), COALESCE(g.capture_date, ''), COALESCE(g.capture_time_source, ''), "
        "COALESCE(g.capture_time_confidence, 0) "
        "FROM global_video_asset g "
        "LEFT JOIN video_analysis_result r ON r.video_key = g.video_key "
        "LEFT JOIN video_analysis_task t ON t.video_key = g.video_key ");
}

QString queryPlaceholders(qsizetype count)
{
    QStringList result;
    result.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        result.append(QStringLiteral("?"));
    }
    return result.join(QLatin1Char(','));
}

bool valueMatches(const QString &value, const QString &required)
{
    const auto normalizedValue = value.simplified().toCaseFolded();
    const auto normalizedRequired = required.simplified().toCaseFolded();
    return !normalizedValue.isEmpty()
        && !normalizedRequired.isEmpty()
        && (normalizedValue == normalizedRequired
            || normalizedValue.contains(normalizedRequired)
            || normalizedRequired.contains(normalizedValue));
}

QStringList entityLabelAliases(const QString &label)
{
    const auto normalized = label.simplified().toCaseFolded();
    const QVector<QStringList> aliasGroups{
        {QStringLiteral("牛仔裤"), QStringLiteral("长裤"), QStringLiteral("裤子"),
         QStringLiteral("丹宁裤")},
        {QStringLiteral("男人"), QStringLiteral("男性"), QStringLiteral("男子"),
         QStringLiteral("男士")},
        {QStringLiteral("女人"), QStringLiteral("女性"), QStringLiteral("女子"),
         QStringLiteral("女士")}
    };
    for (const auto &group : aliasGroups) {
        const bool belongsToGroup = std::any_of(
            group.cbegin(), group.cend(), [&normalized](const QString &alias) {
                return normalized == alias || normalized.contains(alias);
            });
        if (belongsToGroup) {
            return group;
        }
    }
    return normalized.isEmpty() ? QStringList{} : QStringList{normalized};
}

bool entityLabelMatches(const QString &value, const QString &required)
{
    if (valueMatches(value, required)) {
        return true;
    }
    const auto valueAliases = entityLabelAliases(value);
    const auto requiredAliases = entityLabelAliases(required);
    return std::any_of(valueAliases.cbegin(), valueAliases.cend(),
                       [&requiredAliases](const QString &valueAlias) {
        return std::any_of(requiredAliases.cbegin(), requiredAliases.cend(),
                           [&valueAlias](const QString &requiredAlias) {
            return valueMatches(valueAlias, requiredAlias);
        });
    });
}

bool listContainsAll(const QStringList &values, const QStringList &requiredValues)
{
    for (const auto &required : requiredValues) {
        const bool matched = std::any_of(values.cbegin(), values.cend(), [&](const auto &value) {
            return valueMatches(value, required);
        });
        if (!matched) {
            return false;
        }
    }
    return true;
}

bool entityMatchesConstraint(const VisionEntityFact &entity,
                             const StrictEntityConstraint &constraint)
{
    return entityLabelMatches(entity.label, constraint.label)
        && listContainsAll(entity.colors, constraint.colors)
        && listContainsAll(entity.materials, constraint.materials)
        && listContainsAll(entity.attributes, constraint.attributes);
}

bool factsMatchAllConstraints(const QVector<VisionEntityFact> &facts,
                              const QVector<StrictEntityConstraint> &constraints)
{
    for (const auto &constraint : constraints) {
        const bool matched = std::any_of(facts.cbegin(), facts.cend(), [&](const auto &entity) {
            return entityMatchesConstraint(entity, constraint);
        });
        if (!matched) {
            return false;
        }
    }
    return true;
}

bool anyFrameFactsMatchAllConstraints(
    const QVector<QVector<VisionEntityFact>> &factsByFrame,
    const QVector<StrictEntityConstraint> &constraints)
{
    return std::any_of(factsByFrame.cbegin(), factsByFrame.cend(), [&](const auto &facts) {
        return factsMatchAllConstraints(facts, constraints);
    });
}


QString frameDocumentKey(const QString &videoKey, int frameNumber)
{
    return QStringLiteral("frame:%1:%2").arg(videoKey).arg(frameNumber);
}

QString frameSearchableText(const FrameSearchHit &frame)
{
    return QStringList{
        frame.caption,
        frame.tags.join(QLatin1Char(' ')),
        frame.objects.join(QLatin1Char(' ')),
        frame.actions,
        frame.setting,
        VisualAnalysisMetadata::entityFactSearchTerms(frame.entities).join(QLatin1Char(' ')),
        frame.ocrText
    }.join(QLatin1Char(' ')).toCaseFolded();
}

bool frameTextMatchesAllConstraints(const FrameSearchHit &frame,
                                    const QVector<StrictEntityConstraint> &constraints)
{
    const auto searchable = frameSearchableText(frame);
    for (const auto &constraint : constraints) {
        const auto labelAliases = entityLabelAliases(constraint.label);
        const bool labelMatched = std::any_of(
            labelAliases.cbegin(), labelAliases.cend(), [&searchable](const QString &alias) {
                return searchable.contains(alias.simplified().toCaseFolded());
            });
        if (!labelMatched) {
            return false;
        }
        const auto requiredProperties = constraint.colors
            + constraint.materials + constraint.attributes;
        for (const auto &term : requiredProperties) {
            if (!searchable.contains(term.simplified().toCaseFolded())) {
                return false;
            }
        }
    }
    return true;
}

QString matchingFrameEvidenceText(const FrameSearchHit &frame,
                                  const QVector<StrictEntityConstraint> &constraints)
{
    QStringList normalizedTerms;
    for (const auto &constraint : constraints) {
        auto evidenceTerms = entityLabelAliases(constraint.label);
        evidenceTerms.append(constraint.colors);
        evidenceTerms.append(constraint.materials);
        evidenceTerms.append(constraint.attributes);
        for (const auto &term : evidenceTerms) {
            const auto normalized = term.simplified().toCaseFolded();
            if (!normalized.isEmpty() && !normalizedTerms.contains(normalized)) {
                normalizedTerms.append(normalized);
            }
        }
    }

    QStringList matchingParts;
    const auto appendMatchingPart = [&](const QString &label, const QString &value) {
        const auto simplified = value.simplified();
        if (simplified.isEmpty()) {
            return;
        }
        const auto normalized = simplified.toCaseFolded();
        const bool matches = std::any_of(
            normalizedTerms.cbegin(), normalizedTerms.cend(), [&](const auto &term) {
                return normalized.contains(term);
            });
        if (matches) {
            matchingParts.append(QStringLiteral("%1：%2").arg(label, simplified));
        }
    };

    appendMatchingPart(QStringLiteral("描述"), frame.caption);
    appendMatchingPart(QStringLiteral("标签"), frame.tags.join(QStringLiteral("、")));
    appendMatchingPart(QStringLiteral("对象"), frame.objects.join(QStringLiteral("、")));
    appendMatchingPart(QStringLiteral("动作"), frame.actions);
    appendMatchingPart(QStringLiteral("场景"), frame.setting);
    appendMatchingPart(QStringLiteral("实体"),
                       VisualAnalysisMetadata::entityFactSearchTerms(frame.entities)
                           .join(QStringLiteral("、")));
    appendMatchingPart(QStringLiteral("画面文字"), frame.ocrText);

    if (!matchingParts.isEmpty()) {
        return matchingParts.join(QStringLiteral(" · "));
    }
    return frameSearchableText(frame).simplified();
}

struct SameFrameTextEvidence {
    int frameNumber = -1;
    qint64 timestampMs = -1;
    QString caption;
};
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

MaterialSearchResult MaterialCenterQueryService::searchMaterials(
    const QString &naturalLanguageQuery,
    const MaterialSearchScope &scope,
    const QDate &referenceDate,
    const ModelSearchUnderstanding *modelUnderstanding) const
{
    MaterialSearchResult result;
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        result.warningMessage = QStringLiteral("全局素材数据库尚未打开");
        result.reliability = SearchReliabilityEvaluator::evaluate(result);
        return result;
    }
    if (!m_searchEngine) {
        result.warningMessage = QStringLiteral("混合检索引擎尚未初始化");
        result.reliability = SearchReliabilityEvaluator::evaluate(result);
        return result;
    }

    const auto hybrid = m_searchEngine->searchMaterials(naturalLanguageQuery,
                                                         scope,
                                                         referenceDate,
                                                         modelUnderstanding);
    result.parsedQuery = hybrid.parsedQuery;
    result.semanticSearchAvailable = hybrid.semanticSearchAvailable;
    result.warningMessage = hybrid.warningMessage;

    QStringList assetKeys;
    QStringList folderKeys;
    QStringList frameVideoKeys;
    QHash<QString, double> assetScores;
    QHash<QString, double> folderScores;
    QHash<QString, HybridSearchHit> assetHits;
    QHash<QString, HybridSearchHit> folderHits;
    QHash<QString, HybridSearchHit> frameHits;
    for (const auto &hit : hybrid.hits) {
        if (hit.documentType == SearchDocumentType::Asset && !hit.entityKey.isEmpty()) {
            assetKeys.append(hit.entityKey);
            assetScores[hit.entityKey] = std::max(assetScores.value(hit.entityKey), hit.score);
            if (!assetHits.contains(hit.entityKey)
                || hit.score > assetHits.value(hit.entityKey).score) {
                assetHits.insert(hit.entityKey, hit);
            }
        } else if (hit.documentType == SearchDocumentType::Folder && !hit.entityKey.isEmpty()) {
            folderKeys.append(hit.entityKey);
            folderScores[hit.entityKey] = std::max(folderScores.value(hit.entityKey), hit.score);
            if (!folderHits.contains(hit.entityKey)
                || hit.score > folderHits.value(hit.entityKey).score) {
                folderHits.insert(hit.entityKey, hit);
            }
        } else if (hit.documentType == SearchDocumentType::VisualEntity
                   && !hit.entityKey.isEmpty()
                   && !hit.assetEntityKey.isEmpty()
                   && hit.matchedFrameNumber > 0) {
            frameVideoKeys.append(hit.assetEntityKey);
            if (!frameHits.contains(hit.entityKey)
                || hit.score > frameHits.value(hit.entityKey).score) {
                frameHits.insert(hit.entityKey, hit);
            }
        }
    }
    assetKeys.removeDuplicates();
    folderKeys.removeDuplicates();
    frameVideoKeys.removeDuplicates();

    auto db = m_globalDatabaseManager->database();
    QHash<QString, GlobalVideoAsset> assetsByKey;
    constexpr qsizetype batchSize = 400;
    for (qsizetype offset = 0; offset < assetKeys.size(); offset += batchSize) {
        const auto count = std::min(batchSize, assetKeys.size() - offset);
        QSqlQuery query(db);
        query.prepare(assetResultSelectClause()
                      + QStringLiteral(" WHERE g.video_key IN (%1)")
                            .arg(queryPlaceholders(count)));
        for (qsizetype index = 0; index < count; ++index) {
            query.addBindValue(assetKeys.at(offset + index));
        }
        if (!query.exec()) {
            if (!result.warningMessage.isEmpty()) {
                result.warningMessage += QStringLiteral("；");
            }
            result.warningMessage += QStringLiteral("装配素材结果失败：%1")
                                         .arg(query.lastError().text());
            continue;
        }
        while (query.next()) {
            auto asset = readAssetRow(query);
            assetsByKey.insert(asset.videoKey, std::move(asset));
        }
    }

    QHash<QString, FolderSearchHit> foldersByKey;
    for (qsizetype offset = 0; offset < folderKeys.size(); offset += batchSize) {
        const auto count = std::min(batchSize, folderKeys.size() - offset);
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT folder_key, project_uuid, project_name, project_database_path, source_root_id, "
            "source_root_name, name, absolute_path, relative_path, parent_relative_path, depth, "
            "direct_file_count, recursive_file_count, normalized_date, is_available "
            "FROM global_folder_node WHERE folder_key IN (%1)")
                          .arg(queryPlaceholders(count)));
        for (qsizetype index = 0; index < count; ++index) {
            query.addBindValue(folderKeys.at(offset + index));
        }
        if (!query.exec()) {
            if (!result.warningMessage.isEmpty()) {
                result.warningMessage += QStringLiteral("；");
            }
            result.warningMessage += QStringLiteral("装配文件夹结果失败：%1")
                                         .arg(query.lastError().text());
            continue;
        }
        while (query.next()) {
            FolderSearchHit folder;
            folder.folderKey = query.value(0).toString();
            folder.projectUuid = query.value(1).toString();
            folder.projectName = query.value(2).toString();
            folder.projectDatabasePath = query.value(3).toString();
            folder.sourceRootId = query.value(4).toLongLong();
            folder.sourceRootName = query.value(5).toString();
            folder.name = query.value(6).toString();
            folder.absolutePath = query.value(7).toString();
            folder.relativePath = query.value(8).toString();
            folder.parentRelativePath = query.value(9).toString();
            folder.depth = query.value(10).toInt();
            folder.directFileCount = query.value(11).toLongLong();
            folder.recursiveFileCount = query.value(12).toLongLong();
            folder.normalizedDate = query.value(13).toString();
            folder.available = query.value(14).toBool();
            folder.score = folderScores.value(folder.folderKey);
            const auto evidence = folderHits.value(folder.folderKey);
            folder.confidence = evidence.confidence;
            folder.reasons = evidence.reasons;
            foldersByKey.insert(folder.folderKey, std::move(folder));
        }
    }

    QHash<QString, FrameSearchHit> framesByKey;
    for (qsizetype offset = 0; offset < frameVideoKeys.size(); offset += batchSize) {
        const auto count = std::min(batchSize, frameVideoKeys.size() - offset);
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT g.video_key, g.file_name, g.project_name, g.source_root_name, g.relative_path, "
            "g.asset_type, f.frame_number, f.timestamp_ms, COALESCE(f.image_path, ''), "
            "COALESCE(f.caption, ''), COALESCE(f.tags_json, '[]'), COALESCE(f.objects_json, '[]'), "
            "COALESCE(f.actions, ''), COALESCE(f.setting_text, ''), COALESCE(f.entities_json, '[]'), "
            "COALESCE(f.ocr_text, ''), COALESCE(f.structured_profile_version, 1), "
            "COALESCE(f.facts_complete, 0), COALESCE(f.analysis_state, 0), "
            "COALESCE(f.error_message, '') "
            "FROM video_frame_analysis f "
            "JOIN global_video_asset g ON g.video_key = f.video_key "
            "WHERE g.video_key IN (%1)")
                          .arg(queryPlaceholders(count)));
        for (qsizetype index = 0; index < count; ++index) {
            query.addBindValue(frameVideoKeys.at(offset + index));
        }
        if (!query.exec()) {
            if (!result.warningMessage.isEmpty()) {
                result.warningMessage += QStringLiteral("；");
            }
            result.warningMessage += QStringLiteral("装配帧结果失败：%1")
                                         .arg(query.lastError().text());
            continue;
        }
        while (query.next()) {
            const auto key = frameDocumentKey(query.value(0).toString(),
                                              query.value(6).toInt());
            const auto evidence = frameHits.constFind(key);
            if (evidence == frameHits.cend()) {
                continue;
            }
            FrameSearchHit frame;
            frame.frameKey = key;
            frame.videoKey = query.value(0).toString();
            frame.assetKey = frame.videoKey;
            frame.fileName = query.value(1).toString();
            frame.projectName = query.value(2).toString();
            frame.sourceRootName = query.value(3).toString();
            frame.relativePath = query.value(4).toString();
            frame.assetType = static_cast<AssetType>(query.value(5).toInt());
            frame.frameNumber = query.value(6).toInt();
            frame.timestampMs = query.value(7).toLongLong();
            frame.imagePath = query.value(8).toString();
            frame.caption = query.value(9).toString();
            frame.tags = parseJsonList(query.value(10).toString());
            frame.objects = parseJsonList(query.value(11).toString());
            frame.actions = query.value(12).toString();
            frame.setting = query.value(13).toString();
            frame.entities = VisualAnalysisMetadata::entityFactsFromJson(query.value(14).toString());
            frame.ocrText = query.value(15).toString();
            frame.factsComplete = query.value(17).toBool()
                && query.value(16).toInt()
                    >= cinevault::searchconfig::kStructuredVisionProfileVersion
                && static_cast<FrameAnalysisState>(query.value(18).toInt())
                    == FrameAnalysisState::Success
                && query.value(19).toString().trimmed().isEmpty();
            frame.score = evidence->score;
            frame.confidence = evidence->confidence;
            frame.reasons = evidence->reasons;
            framesByKey.insert(key, std::move(frame));
        }
    }

    QSet<QString> assetsWithCompleteFacts;
    QHash<QString, QVector<QVector<VisionEntityFact>>> completeFactsByAssetFrame;
    QHash<QString, SameFrameTextEvidence> incompleteTextEvidenceByAsset;
    if (hybrid.parsedQuery.hasStrictEntityConstraints()) {
        for (qsizetype offset = 0; offset < assetKeys.size(); offset += batchSize) {
            const auto count = std::min(batchSize, assetKeys.size() - offset);
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "SELECT video_key, frame_number, timestamp_ms, COALESCE(caption, ''), "
                "COALESCE(tags_json, '[]'), COALESCE(objects_json, '[]'), COALESCE(actions, ''), "
                "COALESCE(setting_text, ''), COALESCE(entities_json, '[]'), COALESCE(ocr_text, ''), "
                "structured_profile_version, facts_complete, analysis_state, COALESCE(error_message, '') "
                "FROM video_frame_analysis WHERE video_key IN (%1)")
                              .arg(queryPlaceholders(count)));
            for (qsizetype index = 0; index < count; ++index) {
                query.addBindValue(assetKeys.at(offset + index));
            }
            if (!query.exec()) {
                if (!result.warningMessage.isEmpty()) {
                    result.warningMessage += QStringLiteral("；");
                }
                result.warningMessage += QStringLiteral("读取严格实体事实失败：%1")
                                             .arg(query.lastError().text());
                continue;
            }
            while (query.next()) {
                const bool complete = query.value(11).toBool()
                    && query.value(10).toInt()
                        >= cinevault::searchconfig::kStructuredVisionProfileVersion
                    && static_cast<FrameAnalysisState>(query.value(12).toInt())
                        == FrameAnalysisState::Success
                    && query.value(13).toString().trimmed().isEmpty();
                const auto videoKey = query.value(0).toString();
                const auto entities = VisualAnalysisMetadata::entityFactsFromJson(
                    query.value(8).toString());
                if (complete) {
                    assetsWithCompleteFacts.insert(videoKey);
                    completeFactsByAssetFrame[videoKey].append(entities);
                    continue;
                }

                FrameSearchHit frame;
                frame.frameNumber = query.value(1).toInt();
                frame.timestampMs = query.value(2).toLongLong();
                frame.caption = query.value(3).toString();
                frame.tags = parseJsonList(query.value(4).toString());
                frame.objects = parseJsonList(query.value(5).toString());
                frame.actions = query.value(6).toString();
                frame.setting = query.value(7).toString();
                frame.entities = entities;
                frame.ocrText = query.value(9).toString();
                if (!incompleteTextEvidenceByAsset.contains(videoKey)
                    && frameTextMatchesAllConstraints(frame,
                                                      hybrid.parsedQuery.strictEntities)) {
                    SameFrameTextEvidence evidence;
                    evidence.frameNumber = frame.frameNumber;
                    evidence.timestampMs = frame.timestampMs;
                    evidence.caption = matchingFrameEvidenceText(
                        frame, hybrid.parsedQuery.strictEntities);
                    incompleteTextEvidenceByAsset.insert(videoKey, std::move(evidence));
                }
            }
        }
    }

    QSet<QString> foldersWithCompleteFacts;
    QHash<QString, QVector<QVector<VisionEntityFact>>> completeFactsByFolderFrame;
    QSet<QString> foldersWithIncompleteTextEvidence;
    if (hybrid.parsedQuery.hasStrictEntityConstraints()
        && hybrid.parsedQuery.folderByAssetCriteria) {
        for (qsizetype offset = 0; offset < folderKeys.size(); offset += batchSize) {
            const auto count = std::min(batchSize, folderKeys.size() - offset);
            QString sql = QStringLiteral(
                "SELECT g.folder_key, v.frame_number, v.timestamp_ms, COALESCE(v.caption, ''), "
                "COALESCE(v.tags_json, '[]'), COALESCE(v.objects_json, '[]'), COALESCE(v.actions, ''), "
                "COALESCE(v.setting_text, ''), COALESCE(v.entities_json, '[]'), COALESCE(v.ocr_text, ''), "
                "v.structured_profile_version, v.facts_complete, v.analysis_state, COALESCE(v.error_message, '') "
                "FROM global_video_asset g JOIN video_frame_analysis v ON v.video_key = g.video_key "
                "WHERE g.is_available = 1 AND g.folder_key IN (%1)")
                              .arg(queryPlaceholders(count));
            QVariantList binds;
            for (qsizetype index = 0; index < count; ++index) {
                binds.append(folderKeys.at(offset + index));
            }

            QVector<int> typeFilters = hybrid.parsedQuery.assetTypeFilters;
            if (typeFilters.isEmpty() && hybrid.parsedQuery.assetTypeFilter >= 0) {
                typeFilters.append(hybrid.parsedQuery.assetTypeFilter);
            }
            if (scope.assetTypeFilter >= 0) {
                typeFilters = {scope.assetTypeFilter};
            }
            if (!typeFilters.isEmpty()) {
                sql += QStringLiteral(" AND g.asset_type IN (%1)")
                           .arg(queryPlaceholders(typeFilters.size()));
                for (const auto type : std::as_const(typeFilters)) {
                    binds.append(type);
                }
            }
            if (!hybrid.parsedQuery.dateConstraint.isEmpty()) {
                const auto expression = hybrid.parsedQuery.dateConstraint.preferredField
                        == SearchDateField::FileModifiedTime
                    ? QStringLiteral("SUBSTR(g.modified_at, 1, 10)")
                    : QStringLiteral(
                          "COALESCE(NULLIF(g.capture_date, ''), SUBSTR(g.modified_at, 1, 10))");
                if (hybrid.parsedQuery.dateConstraint.isExactDate()) {
                    sql += QStringLiteral(" AND %1 = ?").arg(expression);
                    binds.append(hybrid.parsedQuery.dateConstraint.startDate);
                } else {
                    sql += QStringLiteral(" AND %1 BETWEEN ? AND ?").arg(expression);
                    binds.append(hybrid.parsedQuery.dateConstraint.startDate);
                    binds.append(hybrid.parsedQuery.dateConstraint.endDate);
                }
            }
            if (scope.analysisStatusFilter >= 0) {
                sql += QStringLiteral(" AND g.analysis_status = ?");
                binds.append(scope.analysisStatusFilter);
            }
            if (scope.confirmationStatusFilter >= 0) {
                sql += QStringLiteral(" AND g.confirmation_status = ?");
                binds.append(scope.confirmationStatusFilter);
            }

            QSqlQuery query(db);
            query.prepare(sql);
            for (const auto &bind : std::as_const(binds)) {
                query.addBindValue(bind);
            }
            if (!query.exec()) {
                if (!result.warningMessage.isEmpty()) {
                    result.warningMessage += QStringLiteral("；");
                }
                result.warningMessage += QStringLiteral("读取文件夹内严格实体事实失败：%1")
                                             .arg(query.lastError().text());
                continue;
            }
            while (query.next()) {
                const bool complete = query.value(11).toBool()
                    && query.value(10).toInt()
                        >= cinevault::searchconfig::kStructuredVisionProfileVersion
                    && static_cast<FrameAnalysisState>(query.value(12).toInt())
                        == FrameAnalysisState::Success
                    && query.value(13).toString().trimmed().isEmpty();
                const auto folderKey = query.value(0).toString();
                const auto entities = VisualAnalysisMetadata::entityFactsFromJson(
                    query.value(8).toString());
                if (complete) {
                    foldersWithCompleteFacts.insert(folderKey);
                    completeFactsByFolderFrame[folderKey].append(entities);
                    continue;
                }

                FrameSearchHit frame;
                frame.frameNumber = query.value(1).toInt();
                frame.timestampMs = query.value(2).toLongLong();
                frame.caption = query.value(3).toString();
                frame.tags = parseJsonList(query.value(4).toString());
                frame.objects = parseJsonList(query.value(5).toString());
                frame.actions = query.value(6).toString();
                frame.setting = query.value(7).toString();
                frame.entities = entities;
                frame.ocrText = query.value(9).toString();
                if (frameTextMatchesAllConstraints(frame,
                                                   hybrid.parsedQuery.strictEntities)) {
                    foldersWithIncompleteTextEvidence.insert(folderKey);
                }
            }
        }
    }

    for (const auto &hit : hybrid.hits) {
        if (hit.documentType == SearchDocumentType::VisualEntity) {
            const auto frame = framesByKey.constFind(hit.entityKey);
            if (frame == framesByKey.cend()) {
                continue;
            }
            auto resolvedFrame = frame.value();
            if (hybrid.parsedQuery.hasStrictEntityConstraints()) {
                if (resolvedFrame.factsComplete) {
                    if (!factsMatchAllConstraints(resolvedFrame.entities,
                                                  hybrid.parsedQuery.strictEntities)) {
                        continue;
                    }
                    resolvedFrame.reasons.append(QStringLiteral("同一帧、同一视觉对象属性已验证"));
                } else if (frameTextMatchesAllConstraints(
                               resolvedFrame,
                               hybrid.parsedQuery.strictEntities)) {
                    resolvedFrame.confidence *= 0.82;
                    resolvedFrame.reasons.append(
                        QStringLiteral("同一帧文本证据命中（结构化事实不完整）"));
                } else {
                    ++result.excludedPartialCount;
                    continue;
                }
            }
            result.frames.append(std::move(resolvedFrame));
            continue;
        }
        if (hit.documentType == SearchDocumentType::Folder) {
            const auto folder = foldersByKey.constFind(hit.entityKey);
            if (folder != foldersByKey.cend()) {
                if (hybrid.parsedQuery.hasStrictEntityConstraints()
                    && hybrid.parsedQuery.folderByAssetCriteria) {
                    const bool structuredMatch = foldersWithCompleteFacts.contains(hit.entityKey)
                        && anyFrameFactsMatchAllConstraints(
                            completeFactsByFolderFrame.value(hit.entityKey),
                            hybrid.parsedQuery.strictEntities);
                    const bool textFallback = foldersWithIncompleteTextEvidence.contains(
                        hit.entityKey);
                    if (!structuredMatch && !textFallback) {
                        if (!foldersWithCompleteFacts.contains(hit.entityKey)) {
                            ++result.excludedPartialCount;
                        }
                        continue;
                    }
                }
                auto resolvedFolder = folder.value();
                if (hybrid.parsedQuery.hasStrictEntityConstraints()) {
                    const bool structuredMatch = foldersWithCompleteFacts.contains(hit.entityKey)
                        && anyFrameFactsMatchAllConstraints(
                            completeFactsByFolderFrame.value(hit.entityKey),
                            hybrid.parsedQuery.strictEntities);
                    if (structuredMatch) {
                        resolvedFolder.reasons.append(
                            QStringLiteral("文件夹内同一视觉对象属性已验证"));
                    } else if (foldersWithIncompleteTextEvidence.contains(hit.entityKey)) {
                        resolvedFolder.confidence *= 0.82;
                        resolvedFolder.reasons.append(
                            QStringLiteral("文件夹内同一帧文本证据命中（结构化事实不完整）"));
                    }
                }
                result.folders.append(std::move(resolvedFolder));
            }
            continue;
        }
        if (hit.documentType != SearchDocumentType::Asset) {
            continue;
        }
        const auto asset = assetsByKey.constFind(hit.entityKey);
        if (asset == assetsByKey.cend()) {
            continue;
        }
        if (hybrid.parsedQuery.hasStrictEntityConstraints()) {
            const bool structuredMatch = assetsWithCompleteFacts.contains(hit.entityKey)
                && anyFrameFactsMatchAllConstraints(
                    completeFactsByAssetFrame.value(hit.entityKey),
                    hybrid.parsedQuery.strictEntities);
            if (!structuredMatch && !incompleteTextEvidenceByAsset.contains(hit.entityKey)) {
                if (!assetsWithCompleteFacts.contains(hit.entityKey)) {
                    ++result.excludedPartialCount;
                }
                continue;
            }
        }
        auto resolvedAsset = asset.value();
        const auto evidence = assetHits.value(hit.entityKey);
        resolvedAsset.searchScore = evidence.score;
        resolvedAsset.searchConfidence = evidence.confidence;
        resolvedAsset.searchReasons = evidence.reasons;
        resolvedAsset.matchedFrameNumber = evidence.matchedFrameNumber;
        resolvedAsset.matchedTimestampMs = evidence.matchedTimestampMs;
        resolvedAsset.matchedFrameCaption = evidence.matchedFrameCaption;
        if (hybrid.parsedQuery.hasStrictEntityConstraints()) {
            const bool structuredMatch = assetsWithCompleteFacts.contains(hit.entityKey)
                && anyFrameFactsMatchAllConstraints(
                    completeFactsByAssetFrame.value(hit.entityKey),
                    hybrid.parsedQuery.strictEntities);
            if (structuredMatch) {
                resolvedAsset.searchReasons.append(QStringLiteral("同一视觉对象属性已验证"));
            } else {
                const auto textEvidence = incompleteTextEvidenceByAsset.value(hit.entityKey);
                resolvedAsset.searchConfidence *= 0.82;
                resolvedAsset.searchReasons.append(
                    QStringLiteral("同一帧文本证据命中（结构化事实不完整）"));
                resolvedAsset.matchedFrameNumber = textEvidence.frameNumber;
                resolvedAsset.matchedTimestampMs = textEvidence.timestampMs;
                resolvedAsset.matchedFrameCaption = textEvidence.caption;
            }
        }
        result.assets.append(std::move(resolvedAsset));
    }
    result.reliability = SearchReliabilityEvaluator::evaluate(result);
    return result;
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

    QSqlQuery planQuery(m_globalDatabaseManager->database());
    planQuery.prepare(QStringLiteral(
        "SELECT sampling_policy, frame_interval, structured_profile_version, source_frame_count, planned_frame_count, "
        "asset_size_bytes, asset_modified_at, created_at, updated_at "
        "FROM video_analysis_plan WHERE video_key = ?"));
    planQuery.addBindValue(videoKey.trimmed());
    if (execOrEmpty(planQuery) && planQuery.next()) {
        detail.hasVisualAnalysisPlan = true;
        detail.visualAnalysisPlan.videoKey = videoKey.trimmed();
        detail.visualAnalysisPlan.samplingPolicy = planQuery.value(0).toString();
        detail.visualAnalysisPlan.frameInterval = planQuery.value(1).toInt();
        detail.visualAnalysisPlan.structuredProfileVersion = planQuery.value(2).toInt();
        detail.visualAnalysisPlan.sourceFrameCount = planQuery.value(3).toInt();
        detail.visualAnalysisPlan.plannedFrameCount = planQuery.value(4).toInt();
        detail.visualAnalysisPlan.assetSizeBytes = planQuery.value(5).toLongLong();
        detail.visualAnalysisPlan.assetModifiedAt = planQuery.value(6).toString();
        detail.visualAnalysisPlan.createdAt = planQuery.value(7).toString();
        detail.visualAnalysisPlan.updatedAt = planQuery.value(8).toString();
    }

    QSqlQuery frameQuery(m_globalDatabaseManager->database());
    frameQuery.prepare(QStringLiteral(
        "SELECT id, frame_number, timestamp_ms, COALESCE(image_path, ''), COALESCE(caption, ''), "
        "COALESCE(tags_json, '[]'), COALESCE(objects_json, '[]'), COALESCE(actions, ''), "
        "COALESCE(setting_text, ''), COALESCE(entities_json, '[]'), COALESCE(ocr_text, ''), "
        "COALESCE(ocr_blocks_json, '[]'), COALESCE(structured_profile_version, 1), COALESCE(facts_complete, 0), "
        "COALESCE(model_name, ''), COALESCE(prompt_version, ''), COALESCE(analyzed_at, ''), COALESCE(error_message, ''), "
        "COALESCE(analysis_state, 0), COALESCE(retry_count, 0), COALESCE(last_http_status, 0), COALESCE(last_attempt_at, '') "
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
        frame.entities = VisualAnalysisMetadata::entityFactsFromJson(frameQuery.value(9).toString());
        frame.ocrText = frameQuery.value(10).toString();
        frame.ocrBlocks = parseJsonList(frameQuery.value(11).toString());
        frame.structuredProfileVersion = frameQuery.value(12).toInt();
        frame.factsComplete = frameQuery.value(13).toBool();
        frame.modelName = frameQuery.value(14).toString();
        frame.promptVersion = frameQuery.value(15).toString();
        frame.analyzedAt = frameQuery.value(16).toString();
        frame.errorMessage = frameQuery.value(17).toString();
        frame.analysisState = static_cast<FrameAnalysisState>(frameQuery.value(18).toInt());
        frame.retryCount = frameQuery.value(19).toInt();
        frame.lastHttpStatus = frameQuery.value(20).toInt();
        frame.lastAttemptAt = frameQuery.value(21).toString();
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
