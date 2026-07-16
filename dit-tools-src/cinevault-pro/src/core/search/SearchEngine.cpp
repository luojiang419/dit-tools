#include "core/search/SearchEngine.h"

#include "core/search/SearchQueryUnderstanding.h"

#include "core/search/SemanticSearchProvider.h"
#include "infrastructure/db/GlobalDatabaseManager.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr qsizetype kMaximumResultLimit = 2000;
constexpr qsizetype kMaximumCandidateLimit = 8000;
constexpr qsizetype kSemanticCandidateLimit = 200;

struct SemanticDocumentMetadata {
    SearchDocumentType type = SearchDocumentType::Unknown;
    QString entityKey;
};

QString candidateId(SearchDocumentType type, const QString &entityKey)
{
    return QStringLiteral("%1:%2").arg(static_cast<int>(type)).arg(entityKey);
}

QString canonicalDocumentKey(SearchDocumentType type, const QString &entityKey)
{
    if (type == SearchDocumentType::Folder) {
        return QStringLiteral("folder:%1").arg(entityKey);
    }
    if (type == SearchDocumentType::Asset) {
        return QStringLiteral("asset:%1").arg(entityKey);
    }
    return QStringLiteral("document:%1").arg(entityKey);
}

QString frameDocumentKey(const QString &videoKey, int frameNumber)
{
    return QStringLiteral("frame:%1:%2").arg(videoKey).arg(frameNumber);
}

QString inferredEntityKey(const QString &documentKey, SearchDocumentType type)
{
    const auto expectedPrefix = type == SearchDocumentType::Folder
        ? QStringLiteral("folder:")
        : QStringLiteral("asset:");
    return documentKey.startsWith(expectedPrefix)
        ? documentKey.sliced(expectedPrefix.size())
        : QString();
}

void appendWarning(QString *target, const QString &warning)
{
    const auto normalized = warning.trimmed();
    if (normalized.isEmpty() || target->contains(normalized)) {
        return;
    }
    if (!target->isEmpty()) {
        target->append(QStringLiteral("；"));
    }
    target->append(normalized);
}

qsizetype normalizedLimit(qsizetype requested)
{
    return std::clamp<qsizetype>(requested, 1, kMaximumResultLimit);
}

qsizetype candidateLimit(qsizetype resultLimit)
{
    return std::clamp<qsizetype>(resultLimit * 4, 500, kMaximumCandidateLimit);
}

QString placeholders(qsizetype count)
{
    QStringList values;
    values.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        values.append(QStringLiteral("?"));
    }
    return values.join(QLatin1Char(','));
}

QVector<int> parsedAssetTypes(const ParsedMaterialQuery &parsed);

double textMatchScore(const QStringList &terms,
                      const QString &name,
                      const QString &path,
                      double *pathScore)
{
    if (terms.isEmpty()) {
        return 0.0;
    }
    const auto normalizedName = name.toCaseFolded();
    const auto normalizedPath = path.toCaseFolded();
    double best = 0.0;
    double bestPath = 0.0;
    int searchableTermCount = 0;
    int matchedTermCount = 0;
    int matchedPathTermCount = 0;
    for (const auto &term : terms) {
        const auto normalizedTerm = term.toCaseFolded();
        if (normalizedTerm.isEmpty()) {
            continue;
        }
        ++searchableTermCount;
        bool matched = false;
        if (normalizedName == normalizedTerm) {
            best = std::max(best, 1.0);
            matched = true;
        } else if (normalizedName.contains(normalizedTerm)) {
            best = std::max(best, 0.9);
            matched = true;
        }
        if (normalizedPath == normalizedTerm) {
            bestPath = std::max(bestPath, 1.0);
            best = std::max(best, 0.85);
            matched = true;
            ++matchedPathTermCount;
        } else if (normalizedPath.contains(normalizedTerm)) {
            bestPath = std::max(bestPath, 0.8);
            best = std::max(best, 0.72);
            matched = true;
            ++matchedPathTermCount;
        }
        if (matched) {
            ++matchedTermCount;
        }
    }
    if (searchableTermCount > 0 && matchedTermCount > 0) {
        const auto coverage = static_cast<double>(matchedTermCount)
            / static_cast<double>(searchableTermCount);
        best *= 0.55 + (0.45 * coverage);
        const auto pathCoverage = static_cast<double>(matchedPathTermCount)
            / static_cast<double>(searchableTermCount);
        bestPath *= 0.55 + (0.45 * pathCoverage);
    }
    if (pathScore) {
        *pathScore = bestPath;
    }
    return best;
}

double termCoverageScore(const QStringList &terms, const QString &text)
{
    const auto normalizedText = text.toCaseFolded();
    QSet<QString> uniqueTerms;
    int matched = 0;
    for (const auto &term : terms) {
        const auto normalizedTerm = term.simplified().toCaseFolded();
        if (normalizedTerm.isEmpty() || uniqueTerms.contains(normalizedTerm)) {
            continue;
        }
        uniqueTerms.insert(normalizedTerm);
        if (normalizedText.contains(normalizedTerm)) {
            ++matched;
        }
    }
    return uniqueTerms.isEmpty()
        ? 0.0
        : static_cast<double>(matched) / static_cast<double>(uniqueTerms.size());
}

QVector<SearchLexicalGroup> requiredLexicalGroups(const ParsedMaterialQuery &query)
{
    QVector<SearchLexicalGroup> groups;
    for (const auto &group : query.lexicalGroups) {
        if (group.mode == SearchLexicalGroupMode::Required && !group.alternatives.isEmpty()) {
            groups.append(group);
        }
    }
    return groups;
}

QString groupedFtsQuery(const ParsedMaterialQuery &query,
                        const SearchEngine &engine)
{
    const auto groups = requiredLexicalGroups(query);
    if (groups.isEmpty()) {
        return engine.buildFtsQuery(query.lexicalTerms.join(QLatin1Char(' ')));
    }
    QStringList predicates;
    for (const auto &group : groups) {
        const auto groupQuery = engine.buildFtsQuery(
            group.alternatives.join(QLatin1Char(' ')));
        if (!groupQuery.isEmpty()) {
            predicates.append(QStringLiteral("(%1)").arg(groupQuery));
        }
    }
    return predicates.join(QStringLiteral(" AND "));
}

double lexicalGroupCoverageScore(const QVector<SearchLexicalGroup> &groups,
                                 const QString &text)
{
    const auto normalizedText = text.toCaseFolded();
    int considered = 0;
    int matched = 0;
    for (const auto &group : groups) {
        if (group.mode == SearchLexicalGroupMode::Excluded || group.alternatives.isEmpty()) {
            continue;
        }
        ++considered;
        if (std::any_of(group.alternatives.cbegin(),
                        group.alternatives.cend(),
                        [&normalizedText](const QString &term) {
            return normalizedText.contains(term.simplified().toCaseFolded());
        })) {
            ++matched;
        }
    }
    return considered <= 0 ? 0.0 : static_cast<double>(matched) / considered;
}

void mergeHit(QHash<QString, HybridSearchHit> *hits, HybridSearchHit hit)
{
    const auto key = candidateId(hit.documentType, hit.entityKey);
    auto existing = hits->find(key);
    if (existing == hits->end()) {
        hits->insert(key, std::move(hit));
        return;
    }
    if (existing->documentKey.isEmpty() && !hit.documentKey.isEmpty()) {
        existing->documentKey = hit.documentKey;
    }
    existing->lexicalScore = std::max(existing->lexicalScore, hit.lexicalScore);
    existing->pathScore = std::max(existing->pathScore, hit.pathScore);
    existing->dateScore = std::max(existing->dateScore, hit.dateScore);
    existing->typeScore = std::max(existing->typeScore, hit.typeScore);
    existing->semanticScore = std::max(existing->semanticScore, hit.semanticScore);
    existing->excludedScore = std::max(existing->excludedScore, hit.excludedScore);
    if (hit.dateConfidence > existing->dateConfidence) {
        existing->dateConfidence = hit.dateConfidence;
        existing->dateValue = hit.dateValue;
        existing->dateSource = hit.dateSource;
    }
    for (const auto &reason : std::as_const(hit.reasons)) {
        if (!existing->reasons.contains(reason)) {
            existing->reasons.append(reason);
        }
    }
    if (existing->matchedFrameNumber < 0 && hit.matchedFrameNumber >= 0) {
        existing->matchedFrameNumber = hit.matchedFrameNumber;
        existing->matchedTimestampMs = hit.matchedTimestampMs;
        existing->matchedFrameCaption = hit.matchedFrameCaption;
    }
}

QString dateSourceLabel(const QString &source)
{
    if (source == QStringLiteral("quicktime_creation_date")) return QStringLiteral("QuickTime 拍摄时间");
    if (source == QStringLiteral("exif_datetime_original")) return QStringLiteral("EXIF 原始拍摄时间");
    if (source == QStringLiteral("media_creation_time")) return QStringLiteral("媒体创建时间");
    if (source == QStringLiteral("folder_date")) return QStringLiteral("目录日期推断");
    if (source == QStringLiteral("requested_file_modified_time")) return QStringLiteral("文件修改时间");
    if (source == QStringLiteral("file_modified_time")
        || source == QStringLiteral("legacy_file_modified_time")) {
        return QStringLiteral("文件修改时间兜底");
    }
    return QStringLiteral("日期");
}

QString timestampLabel(qint64 timestampMs)
{
    if (timestampMs < 0) {
        return {};
    }
    const auto totalSeconds = timestampMs / 1000;
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds % 3600) / 60;
    const auto seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

double searchConfidence(const HybridSearchHit &hit,
                        const ParsedMaterialQuery &parsed,
                        const MaterialSearchScope &scope)
{
    double total = 0.0;
    double weight = 0.0;
    if (!parsed.dateConstraint.isEmpty()) {
        total += std::clamp(hit.dateConfidence, 0.0, 1.0);
        weight += 1.0;
    }
    if (!parsedAssetTypes(parsed).isEmpty() || scope.assetTypeFilter >= 0) {
        total += hit.typeScore;
        weight += 1.0;
    }
    if (!parsed.semanticText.trimmed().isEmpty()) {
        total += std::max({hit.lexicalScore, hit.pathScore, hit.semanticScore});
        weight += 1.0;
    }
    if (parsed.resultTarget == SearchResultTarget::Folders
        || parsed.resultTarget == SearchResultTarget::Frames) {
        total += 1.0;
        weight += 1.0;
    }
    if (weight <= 0.0) {
        return std::max({hit.lexicalScore, hit.pathScore, hit.semanticScore});
    }
    return std::clamp(total / weight, 0.0, 1.0);
}

QVector<int> parsedAssetTypes(const ParsedMaterialQuery &parsed)
{
    if (!parsed.assetTypeFilters.isEmpty()) {
        return parsed.assetTypeFilters;
    }
    return parsed.assetTypeFilter >= 0
        ? QVector<int>{parsed.assetTypeFilter}
        : QVector<int>{};
}

void applyResultQuickFilter(ParsedMaterialQuery *parsed,
                            SearchResultQuickFilter quickFilter)
{
    if (!parsed || quickFilter == SearchResultQuickFilter::Smart) {
        return;
    }

    parsed->interpretationLabels.erase(
        std::remove_if(parsed->interpretationLabels.begin(),
                       parsed->interpretationLabels.end(),
                       [](const QString &label) {
            return label.startsWith(QStringLiteral("目标："))
                || label.startsWith(QStringLiteral("类型："))
                || label.startsWith(QStringLiteral("快捷筛选："));
        }),
        parsed->interpretationLabels.end());
    parsed->folderIntent = false;
    parsed->folderByAssetCriteria = false;

    switch (quickFilter) {
    case SearchResultQuickFilter::Video:
        parsed->resultTarget = SearchResultTarget::Assets;
        parsed->frameIntent = false;
        parsed->assetTypeFilters = {static_cast<int>(AssetType::Video)};
        parsed->assetTypeFilter = static_cast<int>(AssetType::Video);
        parsed->interpretationLabels.append(QStringLiteral("快捷筛选：视频"));
        parsed->interpretationLabels.append(QStringLiteral("目标：素材"));
        parsed->interpretationLabels.append(QStringLiteral("类型：视频"));
        break;
    case SearchResultQuickFilter::Frames:
        parsed->resultTarget = SearchResultTarget::Frames;
        parsed->frameIntent = true;
        parsed->assetTypeFilters.clear();
        parsed->assetTypeFilter = -1;
        parsed->interpretationLabels.append(QStringLiteral("快捷筛选：帧画面"));
        parsed->interpretationLabels.append(QStringLiteral("目标：视觉帧"));
        break;
    case SearchResultQuickFilter::Image:
        parsed->resultTarget = SearchResultTarget::Assets;
        parsed->frameIntent = false;
        parsed->assetTypeFilters = {static_cast<int>(AssetType::Image)};
        parsed->assetTypeFilter = static_cast<int>(AssetType::Image);
        parsed->interpretationLabels.append(QStringLiteral("快捷筛选：图片"));
        parsed->interpretationLabels.append(QStringLiteral("目标：素材"));
        parsed->interpretationLabels.append(QStringLiteral("类型：图片"));
        break;
    case SearchResultQuickFilter::Document:
        parsed->resultTarget = SearchResultTarget::Assets;
        parsed->frameIntent = false;
        parsed->assetTypeFilters = {static_cast<int>(AssetType::Document)};
        parsed->assetTypeFilter = static_cast<int>(AssetType::Document);
        parsed->interpretationLabels.append(QStringLiteral("快捷筛选：文档"));
        parsed->interpretationLabels.append(QStringLiteral("目标：素材"));
        parsed->interpretationLabels.append(QStringLiteral("类型：文档"));
        break;
    case SearchResultQuickFilter::Smart:
        break;
    }
}

void appendAssetTypeConstraint(QString *sql,
                               QVariantList *binds,
                               const QString &alias,
                               const MaterialSearchScope &scope,
                               const ParsedMaterialQuery &parsed)
{
    const auto queryTypes = parsedAssetTypes(parsed);
    if (scope.assetTypeFilter >= 0) {
        if (!queryTypes.isEmpty() && !queryTypes.contains(scope.assetTypeFilter)) {
            sql->append(QStringLiteral(" AND 0 = 1"));
            return;
        }
        sql->append(QStringLiteral(" AND %1.asset_type = ?").arg(alias));
        binds->append(scope.assetTypeFilter);
        return;
    }
    if (queryTypes.isEmpty()) {
        return;
    }
    sql->append(QStringLiteral(" AND %1.asset_type IN (%2)")
                    .arg(alias, placeholders(queryTypes.size())));
    for (const auto type : queryTypes) {
        binds->append(type);
    }
}

void appendDateConstraint(QString *sql,
                          QVariantList *binds,
                          const QString &dateExpression,
                          const SearchDateConstraint &constraint)
{
    if (constraint.isEmpty()) {
        return;
    }
    if (constraint.isExactDate()) {
        sql->append(QStringLiteral(" AND %1 = ?").arg(dateExpression));
        binds->append(constraint.startDate);
        return;
    }
    sql->append(QStringLiteral(" AND %1 BETWEEN ? AND ?").arg(dateExpression));
    binds->append(constraint.startDate);
    binds->append(constraint.endDate);
}

QString assetDateExpression(const QString &alias,
                            const SearchDateConstraint &constraint)
{
    if (constraint.preferredField == SearchDateField::FileModifiedTime) {
        return QStringLiteral("SUBSTR(%1.modified_at, 1, 10)").arg(alias);
    }
    return QStringLiteral(
        "COALESCE(NULLIF(%1.capture_date, ''), SUBSTR(%1.modified_at, 1, 10))")
        .arg(alias);
}

void appendAssetOcrConstraint(QString *sql,
                              QVariantList *binds,
                              const QString &assetAlias,
                              const QString &ocrText)
{
    const auto normalized = ocrText.trimmed();
    if (normalized.isEmpty()) {
        return;
    }
    sql->append(QStringLiteral(
        " AND EXISTS (SELECT 1 FROM video_frame_analysis ocr_frame "
        "WHERE ocr_frame.video_key = %1.video_key "
        "AND COALESCE(ocr_frame.ocr_text, '') LIKE ? ESCAPE '\\')")
                    .arg(assetAlias));
    auto pattern = normalized;
    pattern.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    pattern.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    binds->append(QStringLiteral("%") + pattern + QStringLiteral("%"));
}

void appendAssetScope(QString *sql,
                      QVariantList *binds,
                      const MaterialSearchScope &scope,
                      const ParsedMaterialQuery &parsed)
{
    if (!scope.projectUuid.trimmed().isEmpty()) {
        sql->append(QStringLiteral(" AND g.project_uuid = ?"));
        binds->append(scope.projectUuid.trimmed());
    }
    if (!scope.sourceRootName.trimmed().isEmpty()) {
        sql->append(QStringLiteral(" AND g.source_root_name = ?"));
        binds->append(scope.sourceRootName.trimmed());
    }
    if (scope.analysisStatusFilter >= 0) {
        sql->append(QStringLiteral(" AND g.analysis_status = ?"));
        binds->append(scope.analysisStatusFilter);
    }
    if (scope.confirmationStatusFilter >= 0) {
        sql->append(QStringLiteral(" AND g.confirmation_status = ?"));
        binds->append(scope.confirmationStatusFilter);
    }
    appendAssetTypeConstraint(sql, binds, QStringLiteral("g"), scope, parsed);
    appendDateConstraint(sql,
                         binds,
                         assetDateExpression(QStringLiteral("g"), parsed.dateConstraint),
                         parsed.dateConstraint);
    appendAssetOcrConstraint(sql, binds, QStringLiteral("g"), parsed.ocrText);
}

void appendFolderScope(QString *sql,
                       QVariantList *binds,
                       const MaterialSearchScope &scope,
                       const ParsedMaterialQuery &parsed)
{
    if (!scope.projectUuid.trimmed().isEmpty()) {
        sql->append(QStringLiteral(" AND f.project_uuid = ?"));
        binds->append(scope.projectUuid.trimmed());
    }
    if (!scope.sourceRootName.trimmed().isEmpty()) {
        sql->append(QStringLiteral(" AND f.source_root_name = ?"));
        binds->append(scope.sourceRootName.trimmed());
    }
    if (!parsed.folderByAssetCriteria) {
        appendDateConstraint(sql,
                             binds,
                             QStringLiteral("f.normalized_date"),
                             parsed.dateConstraint);
    }
}

void appendFolderAssetCriteria(QString *sql,
                               QVariantList *binds,
                               const MaterialSearchScope &scope,
                               const ParsedMaterialQuery &parsed)
{
    if (!parsed.folderByAssetCriteria) {
        return;
    }
    QString predicate = QStringLiteral(
        "EXISTS (SELECT 1 FROM global_video_asset ga "
        "WHERE ga.folder_key = f.folder_key AND ga.is_available = 1");
    QVariantList predicateBinds;
    if (scope.analysisStatusFilter >= 0) {
        predicate += QStringLiteral(" AND ga.analysis_status = ?");
        predicateBinds.append(scope.analysisStatusFilter);
    }
    if (scope.confirmationStatusFilter >= 0) {
        predicate += QStringLiteral(" AND ga.confirmation_status = ?");
        predicateBinds.append(scope.confirmationStatusFilter);
    }
    appendAssetTypeConstraint(&predicate,
                              &predicateBinds,
                              QStringLiteral("ga"),
                              scope,
                              parsed);
    appendDateConstraint(&predicate,
                         &predicateBinds,
                         assetDateExpression(QStringLiteral("ga"), parsed.dateConstraint),
                         parsed.dateConstraint);
    appendAssetOcrConstraint(&predicate,
                             &predicateBinds,
                             QStringLiteral("ga"),
                             parsed.ocrText);
    predicate += QLatin1Char(')');
    sql->append(QStringLiteral(" AND %1").arg(predicate));
    binds->append(predicateBinds);
}

void appendAssetLexicalPredicate(QString *sql,
                                 QVariantList *binds,
                                 const ParsedMaterialQuery &parsed,
                                 const SearchEngine &engine)
{
    if (parsed.lexicalTerms.isEmpty()) {
        return;
    }
    const QStringList directExpressions{
        QStringLiteral("COALESCE(r.search_text, '')"),
        QStringLiteral("COALESCE(g.source_text, '')"),
        QStringLiteral("COALESCE(g.embedded_metadata_text, '')"),
        QStringLiteral("COALESCE(g.technical_summary, '')"),
        QStringLiteral("g.file_name"),
        QStringLiteral("g.absolute_path"),
        QStringLiteral("g.relative_path"),
        QStringLiteral("COALESCE(g.extension, '')"),
        QStringLiteral("g.project_name"),
        QStringLiteral("g.source_root_name")
    };
    const auto predicateForTerm = [&](const QString &term) {
        const auto pattern = engine.buildLikePattern(term);
        QStringList predicates;
        for (const auto &expression : directExpressions) {
            predicates.append(QStringLiteral("%1 LIKE ? ESCAPE '\\'").arg(expression));
            binds->append(pattern);
        }
        predicates.append(QStringLiteral(
            "EXISTS (SELECT 1 FROM material_dimension_analysis d WHERE d.video_key = g.video_key "
            "AND (COALESCE(d.dimension_name, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(d.detail, '') LIKE ? ESCAPE '\\'))"));
        binds->append(pattern);
        binds->append(pattern);
        predicates.append(QStringLiteral(
            "EXISTS (SELECT 1 FROM video_frame_analysis vfa WHERE vfa.video_key = g.video_key "
            "AND (COALESCE(vfa.caption, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(vfa.tags_json, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(vfa.objects_json, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(vfa.actions, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(vfa.setting_text, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(vfa.entities_json, '') LIKE ? ESCAPE '\\' "
            "OR COALESCE(vfa.ocr_text, '') LIKE ? ESCAPE '\\'))"));
        for (int index = 0; index < 7; ++index) {
            binds->append(pattern);
        }
        return QStringLiteral("(%1)").arg(predicates.join(QStringLiteral(" OR ")));
    };
    const auto requiredGroups = requiredLexicalGroups(parsed);
    QStringList termPredicates;
    if (!requiredGroups.isEmpty()) {
        for (const auto &group : requiredGroups) {
            QStringList alternatives;
            for (const auto &term : group.alternatives) {
                alternatives.append(predicateForTerm(term));
            }
            termPredicates.append(QStringLiteral("(%1)")
                                      .arg(alternatives.join(QStringLiteral(" OR "))));
        }
        sql->append(QStringLiteral(" AND (%1)").arg(termPredicates.join(QStringLiteral(" AND "))));
        return;
    }
    for (const auto &term : parsed.lexicalTerms) {
        termPredicates.append(predicateForTerm(term));
    }
    sql->append(QStringLiteral(" AND (%1)").arg(termPredicates.join(QStringLiteral(" OR "))));
}

void appendFrameLexicalPredicate(QString *sql,
                                 QVariantList *binds,
                                 const ParsedMaterialQuery &parsed,
                                 const SearchEngine &engine)
{
    if (parsed.lexicalTerms.isEmpty()) {
        return;
    }
    const QStringList expressions{
        QStringLiteral("COALESCE(f.caption, '')"),
        QStringLiteral("COALESCE(f.tags_json, '')"),
        QStringLiteral("COALESCE(f.objects_json, '')"),
        QStringLiteral("COALESCE(f.actions, '')"),
        QStringLiteral("COALESCE(f.setting_text, '')"),
        QStringLiteral("COALESCE(f.entities_json, '')"),
        QStringLiteral("COALESCE(f.ocr_text, '')")
    };
    const auto predicateForTerm = [&](const QString &term) {
        const auto pattern = engine.buildLikePattern(term);
        QStringList predicates;
        for (const auto &expression : expressions) {
            predicates.append(QStringLiteral("%1 LIKE ? ESCAPE '\\'").arg(expression));
            binds->append(pattern);
        }
        return QStringLiteral("(%1)").arg(predicates.join(QStringLiteral(" OR ")));
    };
    const auto requiredGroups = requiredLexicalGroups(parsed);
    QStringList termPredicates;
    if (!requiredGroups.isEmpty()) {
        for (const auto &group : requiredGroups) {
            QStringList alternatives;
            for (const auto &term : group.alternatives) {
                alternatives.append(predicateForTerm(term));
            }
            termPredicates.append(QStringLiteral("(%1)")
                                      .arg(alternatives.join(QStringLiteral(" OR "))));
        }
        sql->append(QStringLiteral(" AND (%1)").arg(termPredicates.join(QStringLiteral(" AND "))));
        return;
    }
    for (const auto &term : parsed.lexicalTerms) {
        termPredicates.append(predicateForTerm(term));
    }
    sql->append(QStringLiteral(" AND (%1)").arg(termPredicates.join(QStringLiteral(" OR "))));
}

void appendFrameOcrConstraint(QString *sql,
                              QVariantList *binds,
                              const QString &ocrText,
                              const SearchEngine &engine)
{
    if (ocrText.trimmed().isEmpty()) {
        return;
    }
    sql->append(QStringLiteral(" AND COALESCE(f.ocr_text, '') LIKE ? ESCAPE '\\'"));
    binds->append(engine.buildLikePattern(ocrText));
}

void appendFolderLexicalPredicate(QString *sql,
                                  QVariantList *binds,
                                  const ParsedMaterialQuery &parsed,
                                  const SearchEngine &engine)
{
    if (parsed.lexicalTerms.isEmpty()) {
        return;
    }
    const QStringList expressions{
        QStringLiteral("f.name"),
        QStringLiteral("f.absolute_path"),
        QStringLiteral("f.relative_path"),
        QStringLiteral("f.parent_relative_path"),
        QStringLiteral("f.project_name"),
        QStringLiteral("f.source_root_name")
    };
    const auto predicateForTerm = [&](const QString &term) {
        const auto pattern = engine.buildLikePattern(term);
        QStringList predicates;
        for (const auto &expression : expressions) {
            predicates.append(QStringLiteral("%1 LIKE ? ESCAPE '\\'").arg(expression));
            binds->append(pattern);
        }
        return QStringLiteral("(%1)").arg(predicates.join(QStringLiteral(" OR ")));
    };
    const auto requiredGroups = requiredLexicalGroups(parsed);
    QStringList termPredicates;
    if (!requiredGroups.isEmpty()) {
        for (const auto &group : requiredGroups) {
            QStringList alternatives;
            for (const auto &term : group.alternatives) {
                alternatives.append(predicateForTerm(term));
            }
            termPredicates.append(QStringLiteral("(%1)")
                                      .arg(alternatives.join(QStringLiteral(" OR "))));
        }
        sql->append(QStringLiteral(" AND (%1)").arg(termPredicates.join(QStringLiteral(" AND "))));
        return;
    }
    for (const auto &term : parsed.lexicalTerms) {
        termPredicates.append(predicateForTerm(term));
    }
    sql->append(QStringLiteral(" AND (%1)").arg(termPredicates.join(QStringLiteral(" OR "))));
}

void appendFolderAssetLexicalPredicate(QString *sql,
                                       QVariantList *binds,
                                       const SearchEngine &engine,
                                       const MaterialSearchScope &scope,
                                       const ParsedMaterialQuery &parsed)
{
    if (parsed.lexicalTerms.isEmpty()) {
        return;
    }
    QVariantList predicateBinds;
    const auto predicateForTerm = [&](const QString &term) {
        const auto pattern = engine.buildLikePattern(term);
        const QStringList predicates{
            QStringLiteral("COALESCE(gr.search_text, '') LIKE ? ESCAPE '\\'"),
            QStringLiteral("COALESCE(ga.source_text, '') LIKE ? ESCAPE '\\'"),
            QStringLiteral("COALESCE(ga.embedded_metadata_text, '') LIKE ? ESCAPE '\\'"),
            QStringLiteral("COALESCE(ga.technical_summary, '') LIKE ? ESCAPE '\\'"),
            QStringLiteral("ga.file_name LIKE ? ESCAPE '\\'"),
            QStringLiteral("ga.absolute_path LIKE ? ESCAPE '\\'"),
            QStringLiteral("ga.relative_path LIKE ? ESCAPE '\\'"),
            QStringLiteral("EXISTS (SELECT 1 FROM video_frame_analysis gvfa "
                           "WHERE gvfa.video_key = ga.video_key AND ("
                           "COALESCE(gvfa.caption, '') LIKE ? ESCAPE '\\' OR "
                           "COALESCE(gvfa.tags_json, '') LIKE ? ESCAPE '\\' OR "
                           "COALESCE(gvfa.objects_json, '') LIKE ? ESCAPE '\\' OR "
                           "COALESCE(gvfa.actions, '') LIKE ? ESCAPE '\\' OR "
                           "COALESCE(gvfa.setting_text, '') LIKE ? ESCAPE '\\' OR "
                           "COALESCE(gvfa.entities_json, '') LIKE ? ESCAPE '\\' OR "
                           "COALESCE(gvfa.ocr_text, '') LIKE ? ESCAPE '\\'))")
        };
        for (int index = 0; index < 14; ++index) {
            predicateBinds.append(pattern);
        }
        return QStringLiteral("(%1)").arg(predicates.join(QStringLiteral(" OR ")));
    };
    const auto requiredGroups = requiredLexicalGroups(parsed);
    QStringList termPredicates;
    auto joiner = QStringLiteral(" OR ");
    if (!requiredGroups.isEmpty()) {
        joiner = QStringLiteral(" AND ");
        for (const auto &group : requiredGroups) {
            QStringList alternatives;
            for (const auto &term : group.alternatives) {
                alternatives.append(predicateForTerm(term));
            }
            termPredicates.append(QStringLiteral("(%1)")
                                      .arg(alternatives.join(QStringLiteral(" OR "))));
        }
    } else {
        for (const auto &term : parsed.lexicalTerms) {
            termPredicates.append(predicateForTerm(term));
        }
    }
    QString predicate = QStringLiteral(
        " AND EXISTS (SELECT 1 FROM global_video_asset ga "
        "LEFT JOIN video_analysis_result gr ON gr.video_key = ga.video_key "
        "WHERE ga.folder_key = f.folder_key AND ga.is_available = 1");
    QVariantList structuralBinds;
    appendAssetTypeConstraint(&predicate,
                              &structuralBinds,
                              QStringLiteral("ga"),
                              scope,
                              parsed);
    appendDateConstraint(&predicate,
                         &structuralBinds,
                         assetDateExpression(QStringLiteral("ga"), parsed.dateConstraint),
                         parsed.dateConstraint);
    appendAssetOcrConstraint(&predicate,
                             &structuralBinds,
                             QStringLiteral("ga"),
                             parsed.ocrText);
    predicate += QStringLiteral(" AND (%1))")
                     .arg(termPredicates.join(joiner));
    sql->append(predicate);
    binds->append(structuralBinds);
    binds->append(predicateBinds);
}
}

SearchEngine::SearchEngine(GlobalDatabaseManager *globalDatabaseManager,
                           SemanticSearchProvider *semanticSearchProvider)
    : m_globalDatabaseManager(globalDatabaseManager)
    , m_semanticSearchProvider(semanticSearchProvider)
{
}

QString SearchEngine::buildLikePattern(const QString &keyword) const
{
    auto normalized = keyword.trimmed();
    normalized.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    normalized.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QStringLiteral("%") + normalized + QStringLiteral("%");
}

QString SearchEngine::buildFtsQuery(const QString &keyword) const
{
    const auto tokens = keyword.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList escapedTokens;
    for (auto token : tokens) {
        token.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        escapedTokens.append(QStringLiteral("\"%1\"").arg(token));
    }
    if (escapedTokens.isEmpty()) {
        return {};
    }
    return escapedTokens.join(QStringLiteral(" OR "));
}

void SearchEngine::setMaterialSearchDependencies(GlobalDatabaseManager *globalDatabaseManager,
                                                 SemanticSearchProvider *semanticSearchProvider)
{
    m_globalDatabaseManager = globalDatabaseManager;
    m_semanticSearchProvider = semanticSearchProvider;
}

HybridSearchResult SearchEngine::searchMaterials(const QString &queryText,
                                                  const MaterialSearchScope &scope,
                                                  const QDate &referenceDate,
                                                  const ModelSearchUnderstanding *modelUnderstanding) const
{
    HybridSearchResult result;
    result.parsedQuery = m_queryParser.parse(queryText, referenceDate);
    if (modelUnderstanding) {
        result.parsedQuery = SearchQueryUnderstanding::merge(result.parsedQuery,
                                                             *modelUnderstanding);
    }
    applyResultQuickFilter(&result.parsedQuery, scope.resultQuickFilter);
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()) {
        result.warningMessage = QStringLiteral("全局素材数据库尚未打开");
        return result;
    }

    const auto limit = normalizedLimit(scope.limit);
    const auto lexicalCandidateLimit = candidateLimit(limit);
    auto db = m_globalDatabaseManager->database();
    QHash<QString, HybridSearchHit> mergedHits;
    QHash<QString, QString> bestVisualDocumentByAsset;
    QHash<QString, double> bestVisualDocumentScore;

    QHash<QString, double> ftsScores;
    if (m_globalDatabaseManager->hasFts5() && !result.parsedQuery.lexicalTerms.isEmpty()) {
        QSqlQuery ftsQuery(db);
        ftsQuery.prepare(QStringLiteral(
            "SELECT video_key FROM video_search_fts WHERE video_search_fts MATCH ? "
            "ORDER BY bm25(video_search_fts) LIMIT ?"));
        ftsQuery.addBindValue(groupedFtsQuery(result.parsedQuery, *this));
        ftsQuery.addBindValue(lexicalCandidateLimit);
        if (ftsQuery.exec()) {
            qsizetype rank = 0;
            while (ftsQuery.next()) {
                const auto rankScore = std::max(0.75, 1.0 - (static_cast<double>(rank) * 0.0025));
                ftsScores.insert(ftsQuery.value(0).toString(), rankScore);
                ++rank;
            }
        } else {
            appendWarning(&result.warningMessage,
                          QStringLiteral("FTS 查询失败：%1").arg(ftsQuery.lastError().text()));
        }
    }

    const auto appendAssets = [&](const QStringList &restrictedEntityKeys,
                                  bool requireLexical,
                                  const QHash<QString, double> &semanticScores) {
        QString sql = QStringLiteral(
            "SELECT g.video_key, g.file_name, g.absolute_path, g.relative_path, g.modified_at, g.asset_type, "
            "COALESCE(g.capture_date, ''), COALESCE(g.capture_time_source, ''), "
            "COALESCE(g.capture_time_confidence, 0), COALESCE(r.search_text, ''), "
            "COALESCE(g.source_text, ''), COALESCE(g.technical_summary, '') "
            "FROM global_video_asset g "
            "LEFT JOIN video_analysis_result r ON r.video_key = g.video_key "
            "WHERE g.is_available = 1");
        QVariantList binds;
        appendAssetScope(&sql, &binds, scope, result.parsedQuery);
        if (!restrictedEntityKeys.isEmpty()) {
            sql += QStringLiteral(" AND g.video_key IN (%1)")
                       .arg(placeholders(restrictedEntityKeys.size()));
            for (const auto &key : restrictedEntityKeys) {
                binds.append(key);
            }
        }
        if (requireLexical) {
            appendAssetLexicalPredicate(&sql,
                                        &binds,
                                        result.parsedQuery,
                                        *this);
        }
        sql += QStringLiteral(" ORDER BY g.video_key LIMIT ?");
        binds.append(restrictedEntityKeys.isEmpty()
                         ? lexicalCandidateLimit
                         : restrictedEntityKeys.size());

        QSqlQuery query(db);
        query.prepare(sql);
        for (const auto &bind : std::as_const(binds)) {
            query.addBindValue(bind);
        }
        if (!query.exec()) {
            appendWarning(&result.warningMessage,
                          QStringLiteral("素材候选查询失败：%1").arg(query.lastError().text()));
            return;
        }
        while (query.next()) {
            HybridSearchHit hit;
            hit.documentType = SearchDocumentType::Asset;
            hit.entityKey = query.value(0).toString();
            hit.documentKey = canonicalDocumentKey(hit.documentType, hit.entityKey);
            double pathScore = 0.0;
            hit.lexicalScore = textMatchScore(result.parsedQuery.lexicalTerms,
                                              query.value(1).toString(),
                                              query.value(2).toString() + QLatin1Char(' ')
                                                  + query.value(3).toString(),
                                              &pathScore);
            if (requireLexical && !result.parsedQuery.lexicalTerms.isEmpty()) {
                hit.lexicalScore = std::max(hit.lexicalScore, 0.6);
            }
            hit.pathScore = pathScore;
            const auto contentCoverage = termCoverageScore(
                result.parsedQuery.lexicalTerms,
                QStringList{query.value(9).toString(),
                            query.value(10).toString(),
                            query.value(11).toString()}.join(QLatin1Char(' ')));
            const auto assetSearchable = QStringList{
                query.value(1).toString(), query.value(2).toString(),
                query.value(3).toString(), query.value(9).toString(),
                query.value(10).toString(), query.value(11).toString()
            }.join(QLatin1Char(' '));
            const auto groupCoverage = lexicalGroupCoverageScore(
                result.parsedQuery.lexicalGroups,
                assetSearchable);
            const auto effectiveCoverage = std::max(contentCoverage, groupCoverage);
            if (effectiveCoverage > 0.0) {
                hit.lexicalScore = std::max(hit.lexicalScore,
                                            0.4 + (0.6 * effectiveCoverage));
                if (effectiveCoverage >= 0.999) {
                    hit.reasons.append(QStringLiteral("查询关键词完整覆盖"));
                }
            }
            hit.excludedScore = termCoverageScore(result.parsedQuery.excludedTerms,
                                                  assetSearchable);
            hit.lexicalScore = std::max(hit.lexicalScore, ftsScores.value(hit.entityKey));
            if (requireLexical && !result.parsedQuery.lexicalTerms.isEmpty()) {
                hit.reasons.append(QStringLiteral("关键词或视觉文本命中"));
            }
            if (!result.parsedQuery.ocrText.isEmpty()) {
                hit.reasons.append(
                    QStringLiteral("画面 OCR 命中：%1").arg(result.parsedQuery.ocrText));
            }
            if (!result.parsedQuery.dateConstraint.isEmpty()) {
                if (result.parsedQuery.dateConstraint.preferredField
                    == SearchDateField::FileModifiedTime) {
                    hit.dateValue = query.value(4).toString().left(10);
                    hit.dateSource = QStringLiteral("requested_file_modified_time");
                    hit.dateConfidence = 1.0;
                } else {
                    hit.dateValue = query.value(6).toString();
                    hit.dateSource = query.value(7).toString();
                    hit.dateConfidence = query.value(8).toDouble();
                    if (hit.dateValue.isEmpty()) {
                        hit.dateValue = query.value(4).toString().left(10);
                        hit.dateSource = QStringLiteral("legacy_file_modified_time");
                        hit.dateConfidence = 0.25;
                    }
                }
                hit.dateScore = std::clamp(hit.dateConfidence, 0.0, 1.0);
                hit.reasons.append(QStringLiteral("日期命中：%1（%2）")
                                       .arg(hit.dateValue, dateSourceLabel(hit.dateSource)));
            }
            hit.typeScore = (!result.parsedQuery.assetTypeFilters.isEmpty()
                             || result.parsedQuery.assetTypeFilter >= 0
                             || scope.assetTypeFilter >= 0)
                ? 1.0
                : 0.0;
            if (hit.typeScore > 0.0) {
                hit.reasons.append(QStringLiteral("素材类型命中"));
            }
            hit.semanticScore = semanticScores.value(hit.entityKey);
            mergeHit(&mergedHits, std::move(hit));
        }
    };

    const auto appendFolders = [&](const QStringList &restrictedEntityKeys,
                                   bool requireLexical,
                                   const QHash<QString, double> &semanticScores) {
        QString sql = QStringLiteral(
            "SELECT f.folder_key, f.name, f.absolute_path, f.relative_path, f.normalized_date "
            "FROM global_folder_node f WHERE f.is_available = 1");
        QVariantList binds;
        appendFolderScope(&sql, &binds, scope, result.parsedQuery);
        appendFolderAssetCriteria(&sql, &binds, scope, result.parsedQuery);
        if (!restrictedEntityKeys.isEmpty()) {
            sql += QStringLiteral(" AND f.folder_key IN (%1)")
                       .arg(placeholders(restrictedEntityKeys.size()));
            for (const auto &key : restrictedEntityKeys) {
                binds.append(key);
            }
        }
        if (requireLexical) {
            if (result.parsedQuery.folderByAssetCriteria) {
                appendFolderAssetLexicalPredicate(&sql,
                                                  &binds,
                                                  *this,
                                                  scope,
                                                  result.parsedQuery);
            } else {
                appendFolderLexicalPredicate(&sql,
                                             &binds,
                                             result.parsedQuery,
                                             *this);
            }
        }
        sql += QStringLiteral(" ORDER BY f.folder_key LIMIT ?");
        binds.append(restrictedEntityKeys.isEmpty()
                         ? lexicalCandidateLimit
                         : restrictedEntityKeys.size());

        QSqlQuery query(db);
        query.prepare(sql);
        for (const auto &bind : std::as_const(binds)) {
            query.addBindValue(bind);
        }
        if (!query.exec()) {
            appendWarning(&result.warningMessage,
                          QStringLiteral("文件夹候选查询失败：%1").arg(query.lastError().text()));
            return;
        }
        while (query.next()) {
            HybridSearchHit hit;
            hit.documentType = SearchDocumentType::Folder;
            hit.entityKey = query.value(0).toString();
            hit.documentKey = canonicalDocumentKey(hit.documentType, hit.entityKey);
            double pathScore = 0.0;
            hit.lexicalScore = textMatchScore(result.parsedQuery.lexicalTerms,
                                              query.value(1).toString(),
                                              query.value(2).toString() + QLatin1Char(' ')
                                                  + query.value(3).toString(),
                                              &pathScore);
            const auto folderSearchable = QStringList{
                query.value(1).toString(), query.value(2).toString(),
                query.value(3).toString()
            }.join(QLatin1Char(' '));
            const auto groupCoverage = lexicalGroupCoverageScore(
                result.parsedQuery.lexicalGroups,
                folderSearchable);
            if (groupCoverage > 0.0) {
                hit.lexicalScore = std::max(hit.lexicalScore,
                                            0.4 + (0.6 * groupCoverage));
            }
            hit.excludedScore = termCoverageScore(result.parsedQuery.excludedTerms,
                                                  folderSearchable);
            if (requireLexical && !result.parsedQuery.lexicalTerms.isEmpty()) {
                hit.lexicalScore = std::max(hit.lexicalScore, 0.6);
                hit.reasons.append(result.parsedQuery.folderByAssetCriteria
                    ? QStringLiteral("文件夹内素材内容命中")
                    : QStringLiteral("文件夹名称或路径命中"));
            }
            hit.pathScore = pathScore;
            if (!result.parsedQuery.dateConstraint.isEmpty()) {
                hit.dateValue = result.parsedQuery.folderByAssetCriteria
                    ? result.parsedQuery.dateConstraint.startDate
                    : query.value(4).toString();
                hit.dateSource = result.parsedQuery.folderByAssetCriteria
                    ? QStringLiteral("matching_asset_date")
                    : QStringLiteral("folder_date");
                hit.dateConfidence = result.parsedQuery.folderByAssetCriteria ? 0.70 : 0.85;
                hit.dateScore = hit.dateConfidence;
                hit.reasons.append(result.parsedQuery.folderByAssetCriteria
                    ? QStringLiteral("文件夹内素材日期命中")
                    : QStringLiteral("目录日期命中：%1").arg(hit.dateValue));
            }
            hit.typeScore = (!result.parsedQuery.assetTypeFilters.isEmpty()
                             || result.parsedQuery.assetTypeFilter >= 0
                             || scope.assetTypeFilter >= 0)
                ? 1.0
                : 0.0;
            if (hit.typeScore > 0.0) {
                hit.reasons.append(QStringLiteral("文件夹内素材类型命中"));
            }
            hit.semanticScore = semanticScores.value(hit.entityKey);
            mergeHit(&mergedHits, std::move(hit));
        }
    };

    const auto appendFrames = [&](const QStringList &restrictedDocumentKeys,
                                  bool requireLexical,
                                  const QHash<QString, double> &semanticScores) {
        QString sql = QStringLiteral(
            "SELECT g.video_key, f.frame_number, f.timestamp_ms, COALESCE(f.image_path, ''), "
            "COALESCE(f.caption, ''), COALESCE(f.tags_json, ''), COALESCE(f.objects_json, ''), "
            "COALESCE(f.actions, ''), COALESCE(f.setting_text, ''), COALESCE(f.entities_json, ''), "
            "COALESCE(f.ocr_text, ''), g.file_name, g.absolute_path, g.relative_path, g.modified_at, "
            "g.asset_type, COALESCE(g.capture_date, ''), COALESCE(g.capture_time_source, ''), "
            "COALESCE(g.capture_time_confidence, 0) "
            "FROM video_frame_analysis f "
            "JOIN global_video_asset g ON g.video_key = f.video_key "
            "WHERE g.is_available = 1 AND f.analysis_state = 1 "
            "AND TRIM(COALESCE(f.error_message, '')) = ''");
        QVariantList binds;
        appendAssetScope(&sql, &binds, scope, result.parsedQuery);
        appendFrameOcrConstraint(&sql,
                                 &binds,
                                 result.parsedQuery.ocrText,
                                 *this);
        if (!restrictedDocumentKeys.isEmpty()) {
            sql += QStringLiteral(
                " AND ('frame:' || f.video_key || ':' || f.frame_number) IN (%1)")
                       .arg(placeholders(restrictedDocumentKeys.size()));
            for (const auto &key : restrictedDocumentKeys) {
                binds.append(key);
            }
        }
        if (requireLexical) {
            appendFrameLexicalPredicate(&sql,
                                        &binds,
                                        result.parsedQuery,
                                        *this);
        }
        sql += QStringLiteral(" ORDER BY g.video_key, f.frame_number LIMIT ?");
        binds.append(restrictedDocumentKeys.isEmpty()
                         ? lexicalCandidateLimit
                         : restrictedDocumentKeys.size());

        QSqlQuery query(db);
        query.prepare(sql);
        for (const auto &bind : std::as_const(binds)) {
            query.addBindValue(bind);
        }
        if (!query.exec()) {
            appendWarning(&result.warningMessage,
                          QStringLiteral("帧候选查询失败：%1").arg(query.lastError().text()));
            return;
        }
        while (query.next()) {
            HybridSearchHit hit;
            hit.documentType = SearchDocumentType::VisualEntity;
            hit.assetEntityKey = query.value(0).toString();
            hit.matchedFrameNumber = query.value(1).toInt();
            hit.matchedTimestampMs = query.value(2).toLongLong();
            hit.matchedFrameCaption = query.value(4).toString().trimmed();
            if (hit.matchedFrameCaption.isEmpty()) {
                hit.matchedFrameCaption = query.value(10).toString().trimmed();
            }
            hit.documentKey = frameDocumentKey(hit.assetEntityKey,
                                               hit.matchedFrameNumber);
            hit.entityKey = hit.documentKey;

            double pathScore = 0.0;
            const auto frameText = QStringList{
                query.value(4).toString(), query.value(5).toString(),
                query.value(6).toString(), query.value(7).toString(),
                query.value(8).toString(), query.value(9).toString(),
                query.value(10).toString()
            }.join(QLatin1Char(' '));
            hit.lexicalScore = textMatchScore(result.parsedQuery.lexicalTerms,
                                              frameText,
                                              query.value(11).toString() + QLatin1Char(' ')
                                                  + query.value(12).toString() + QLatin1Char(' ')
                                                  + query.value(13).toString(),
                                              &pathScore);
            const auto groupCoverage = lexicalGroupCoverageScore(
                result.parsedQuery.lexicalGroups,
                frameText);
            if (groupCoverage > 0.0) {
                hit.lexicalScore = std::max(hit.lexicalScore,
                                            0.4 + (0.6 * groupCoverage));
            }
            hit.excludedScore = termCoverageScore(result.parsedQuery.excludedTerms,
                                                  frameText);
            if (requireLexical && !result.parsedQuery.lexicalTerms.isEmpty()) {
                hit.lexicalScore = std::max(hit.lexicalScore, 0.68);
                hit.reasons.append(QStringLiteral("帧视觉事实或文字命中"));
            }
            hit.pathScore = pathScore;
            hit.semanticScore = semanticScores.value(hit.documentKey);
            if (!result.parsedQuery.ocrText.isEmpty()) {
                hit.reasons.append(
                    QStringLiteral("帧 OCR 命中：%1").arg(result.parsedQuery.ocrText));
            }
            if (!result.parsedQuery.dateConstraint.isEmpty()) {
                if (result.parsedQuery.dateConstraint.preferredField
                    == SearchDateField::FileModifiedTime) {
                    hit.dateValue = query.value(14).toString().left(10);
                    hit.dateSource = QStringLiteral("requested_file_modified_time");
                    hit.dateConfidence = 1.0;
                } else {
                    hit.dateValue = query.value(16).toString();
                    hit.dateSource = query.value(17).toString();
                    hit.dateConfidence = query.value(18).toDouble();
                    if (hit.dateValue.isEmpty()) {
                        hit.dateValue = query.value(14).toString().left(10);
                        hit.dateSource = QStringLiteral("legacy_file_modified_time");
                        hit.dateConfidence = 0.25;
                    }
                }
                hit.dateScore = std::clamp(hit.dateConfidence, 0.0, 1.0);
                hit.reasons.append(QStringLiteral("所属素材日期命中：%1（%2）")
                                       .arg(hit.dateValue, dateSourceLabel(hit.dateSource)));
            }
            hit.typeScore = (!result.parsedQuery.assetTypeFilters.isEmpty()
                             || result.parsedQuery.assetTypeFilter >= 0
                             || scope.assetTypeFilter >= 0)
                ? 1.0
                : 0.0;
            if (hit.typeScore > 0.0) {
                hit.reasons.append(QStringLiteral("所属素材类型命中"));
            }
            hit.reasons.append(QStringLiteral("帧位置：%1")
                                   .arg(timestampLabel(hit.matchedTimestampMs)));
            mergeHit(&mergedHits, std::move(hit));
        }
    };

    const bool hasLexicalTerms = !result.parsedQuery.lexicalTerms.isEmpty();
    if (result.parsedQuery.resultTarget == SearchResultTarget::Assets) {
        appendAssets({}, hasLexicalTerms, {});
    } else if (result.parsedQuery.resultTarget == SearchResultTarget::Folders) {
        appendFolders({}, hasLexicalTerms, {});
    } else {
        appendFrames({}, hasLexicalTerms, {});
    }

    if (m_semanticSearchProvider
        && (!result.parsedQuery.semanticText.trimmed().isEmpty()
            || !result.parsedQuery.semanticVariants.isEmpty())) {
        QHash<QString, double> similarities;
        bool semanticRequestSucceeded = false;
        const auto collectSemanticHits = [&](const QString &text, double weight) {
            if (text.trimmed().isEmpty()) {
                return;
            }
            QString semanticError;
            const auto hits = m_semanticSearchProvider->search(
                text,
                std::min<qsizetype>(kSemanticCandidateLimit, lexicalCandidateLimit),
                &semanticError);
            if (!semanticError.isEmpty()) {
                appendWarning(&result.warningMessage,
                              QStringLiteral("语义搜索不可用：%1").arg(semanticError));
                return;
            }
            semanticRequestSucceeded = true;
            for (const auto &hit : hits) {
                const auto score = std::clamp((hit.similarity + 1.0) / 2.0,
                                              0.0,
                                              1.0)
                    * std::clamp(weight, 0.1, 1.0);
                similarities[hit.documentKey] = std::max(
                    similarities.value(hit.documentKey),
                    score);
            }
        };
        collectSemanticHits(result.parsedQuery.semanticText, 1.0);
        for (const auto &variant : result.parsedQuery.semanticVariants) {
            collectSemanticHits(variant.text, std::min(variant.weight, 0.85));
        }
        result.semanticSearchAvailable = semanticRequestSucceeded
            && m_semanticSearchProvider->isReady();
        if (!similarities.isEmpty()) {
            QStringList documentKeys = similarities.keys();

            QHash<QString, SemanticDocumentMetadata> metadata;
            QSqlQuery metadataQuery(db);
            metadataQuery.prepare(QStringLiteral(
                "SELECT document_key, document_type, entity_key FROM search_document "
                "WHERE document_key IN (%1)").arg(placeholders(documentKeys.size())));
            for (const auto &key : std::as_const(documentKeys)) {
                metadataQuery.addBindValue(key);
            }
            if (metadataQuery.exec()) {
                while (metadataQuery.next()) {
                    SemanticDocumentMetadata item;
                    item.type = static_cast<SearchDocumentType>(metadataQuery.value(1).toInt());
                    item.entityKey = metadataQuery.value(2).toString().trimmed();
                    if (item.entityKey.isEmpty()) {
                        item.entityKey = inferredEntityKey(metadataQuery.value(0).toString(), item.type);
                    }
                    metadata.insert(metadataQuery.value(0).toString(), std::move(item));
                }
            } else {
                appendWarning(&result.warningMessage,
                              QStringLiteral("语义结果映射失败：%1")
                                  .arg(metadataQuery.lastError().text()));
            }

            QStringList assetKeys;
            QStringList folderKeys;
            QStringList frameDocumentKeys;
            QHash<QString, double> assetScores;
            QHash<QString, double> folderScores;
            QHash<QString, double> frameScores;
            for (const auto &documentKey : std::as_const(documentKeys)) {
                const auto item = metadata.constFind(documentKey);
                if (item == metadata.cend() || item->entityKey.isEmpty()) {
                    continue;
                }
                if (result.parsedQuery.resultTarget == SearchResultTarget::Frames) {
                    if (item->type == SearchDocumentType::VisualEntity) {
                        frameDocumentKeys.append(documentKey);
                        frameScores[documentKey] = std::max(
                            frameScores.value(documentKey), similarities.value(documentKey));
                    }
                    continue;
                }
                if (item->type == SearchDocumentType::Asset
                    || item->type == SearchDocumentType::VisualEntity) {
                    assetKeys.append(item->entityKey);
                    assetScores[item->entityKey] = std::max(
                        assetScores.value(item->entityKey), similarities.value(documentKey));
                    if (item->type == SearchDocumentType::VisualEntity
                        && similarities.value(documentKey)
                            > bestVisualDocumentScore.value(item->entityKey)) {
                        bestVisualDocumentScore[item->entityKey] = similarities.value(documentKey);
                        bestVisualDocumentByAsset[item->entityKey] = documentKey;
                    }
                } else if (item->type == SearchDocumentType::Folder) {
                    folderKeys.append(item->entityKey);
                    folderScores[item->entityKey] = std::max(
                        folderScores.value(item->entityKey), similarities.value(documentKey));
                }
            }
            assetKeys.removeDuplicates();
            folderKeys.removeDuplicates();
            frameDocumentKeys.removeDuplicates();
            if (result.parsedQuery.folderByAssetCriteria) {
                folderKeys.clear();
                folderScores.clear();
            }
            if (result.parsedQuery.resultTarget == SearchResultTarget::Assets
                && !assetKeys.isEmpty()) {
                appendAssets(assetKeys, false, assetScores);
            }
            if (result.parsedQuery.resultTarget == SearchResultTarget::Folders) {
                if (result.parsedQuery.folderByAssetCriteria && !assetKeys.isEmpty()) {
                    QSqlQuery folderMapQuery(db);
                    folderMapQuery.prepare(QStringLiteral(
                        "SELECT video_key, folder_key FROM global_video_asset "
                        "WHERE is_available = 1 AND folder_key <> '' AND video_key IN (%1)")
                                               .arg(placeholders(assetKeys.size())));
                    for (const auto &assetKey : std::as_const(assetKeys)) {
                        folderMapQuery.addBindValue(assetKey);
                    }
                    if (folderMapQuery.exec()) {
                        while (folderMapQuery.next()) {
                            const auto assetKey = folderMapQuery.value(0).toString();
                            const auto folderKey = folderMapQuery.value(1).toString();
                            if (folderKey.isEmpty()) {
                                continue;
                            }
                            folderKeys.append(folderKey);
                            folderScores[folderKey] = std::max(folderScores.value(folderKey),
                                                               assetScores.value(assetKey));
                        }
                    } else {
                        appendWarning(&result.warningMessage,
                                      QStringLiteral("语义素材所属文件夹映射失败：%1")
                                          .arg(folderMapQuery.lastError().text()));
                    }
                }
                folderKeys.removeDuplicates();
                if (!folderKeys.isEmpty()) {
                    appendFolders(folderKeys, false, folderScores);
                }
            }
            if (result.parsedQuery.resultTarget == SearchResultTarget::Frames
                && !frameDocumentKeys.isEmpty()) {
                appendFrames(frameDocumentKeys, false, frameScores);
            }
        }
    }

    QSqlQuery visualEvidenceQuery(db);
    visualEvidenceQuery.prepare(QStringLiteral(
        "SELECT timestamp_ms, COALESCE(caption, ''), COALESCE(ocr_text, '') "
        "FROM video_frame_analysis WHERE video_key = ? AND frame_number = ? LIMIT 1"));
    for (auto iterator = bestVisualDocumentByAsset.cbegin();
         iterator != bestVisualDocumentByAsset.cend();
         ++iterator) {
        auto merged = mergedHits.find(candidateId(SearchDocumentType::Asset, iterator.key()));
        if (merged == mergedHits.end()) {
            continue;
        }
        const auto separator = iterator.value().lastIndexOf(QLatin1Char(':'));
        bool frameNumberValid = false;
        const auto frameNumber = separator >= 0
            ? iterator.value().mid(separator + 1).toInt(&frameNumberValid)
            : -1;
        if (!frameNumberValid) {
            continue;
        }
        visualEvidenceQuery.bindValue(0, iterator.key());
        visualEvidenceQuery.bindValue(1, frameNumber);
        if (!visualEvidenceQuery.exec() || !visualEvidenceQuery.next()) {
            visualEvidenceQuery.finish();
            continue;
        }
        merged->matchedFrameNumber = frameNumber;
        merged->matchedTimestampMs = visualEvidenceQuery.value(0).toLongLong();
        merged->matchedFrameCaption = visualEvidenceQuery.value(1).toString().trimmed();
        if (merged->matchedFrameCaption.isEmpty()) {
            merged->matchedFrameCaption = visualEvidenceQuery.value(2).toString().trimmed();
        }
        merged->reasons.append(QStringLiteral("视觉帧命中：%1")
                                   .arg(timestampLabel(merged->matchedTimestampMs)));
        visualEvidenceQuery.finish();
    }

    result.hits.reserve(mergedHits.size());
    for (auto hit : std::as_const(mergedHits)) {
        hit.score = (0.42 * hit.lexicalScore)
            + (0.16 * hit.pathScore)
            + (0.30 * hit.semanticScore)
            + (0.07 * hit.dateScore)
            + (0.05 * hit.typeScore)
            - (0.20 * hit.excludedScore);
        if (result.parsedQuery.folderIntent
            && hit.documentType == SearchDocumentType::Folder) {
            hit.score += 0.08;
        }
        if (hit.semanticScore > 0.0) {
            const auto reason = hit.matchedFrameNumber >= 0
                ? QStringLiteral("视觉语义命中")
                : QStringLiteral("本地语义命中");
            if (!hit.reasons.contains(reason)) {
                hit.reasons.append(reason);
            }
        }
        if (hit.excludedScore > 0.0) {
            hit.reasons.append(QStringLiteral("命中用户明确排除项，已降低排序"));
        }
        hit.confidence = searchConfidence(hit, result.parsedQuery, scope);
        hit.score = std::clamp(hit.score, 0.0, 1.0);
        result.hits.append(std::move(hit));
    }
    std::sort(result.hits.begin(), result.hits.end(), [&](const auto &left, const auto &right) {
        if (std::abs(left.score - right.score) > 0.000001) {
            return left.score > right.score;
        }
        if (result.parsedQuery.folderIntent && left.documentType != right.documentType) {
            return left.documentType == SearchDocumentType::Folder;
        }
        if (left.documentType != right.documentType) {
            return left.documentType == SearchDocumentType::Asset;
        }
        return left.entityKey.compare(right.entityKey, Qt::CaseInsensitive) < 0;
    });
    if (result.hits.size() > limit) {
        result.hits.resize(limit);
    }
    return result;
}
