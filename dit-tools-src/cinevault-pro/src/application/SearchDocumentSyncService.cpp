#include "application/SearchDocumentSyncService.h"

#include "application/IndexingWorkCoordinator.h"

#include "core/search/SemanticSearchIndexService.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/FolderPathMetadata.h"
#include "shared/Formatters.h"

#include <QtConcurrent>

#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimer>

#include <limits>
#include <functional>
#include <utility>

namespace {
void appendText(QStringList *parts, const QVariant &value)
{
    const auto text = value.toString().simplified();
    if (!text.isEmpty()) {
        parts->append(text);
    }
}

void appendTexts(QStringList *parts, const QList<QVariant> &values)
{
    for (const auto &value : values) {
        appendText(parts, value);
    }
}

void updateSourceTimestamp(SearchDocumentInput *input, const QString &timestamp)
{
    const auto normalized = timestamp.trimmed();
    if (normalized > input->sourceUpdatedAt) {
        input->sourceUpdatedAt = normalized;
    }
}

bool executeQuery(QSqlQuery *query, const QString &context, QString *errorMessage)
{
    if (query->exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("%1：%2").arg(context, query->lastError().text());
    }
    return false;
}

constexpr qsizetype SearchDocumentBatchSize = 500;
constexpr qsizetype AssetCollectionPageSize = 64;
constexpr qsizetype MaxPendingSearchChanges = 5000;
constexpr qsizetype MaxAggregateCharacters = 131072;

using SearchDocumentBatchConsumer = std::function<bool(QVector<SearchDocumentInput> &&,
                                                        QString *)>;

void mergeUpdateResult(SemanticIndexUpdateResult *target,
                       const SemanticIndexUpdateResult &source)
{
    if (!target) {
        return;
    }
    target->inserted += source.inserted;
    target->updated += source.updated;
    target->unchanged += source.unchanged;
    target->removed += source.removed;
    target->rebuilt = target->rebuilt || source.rebuilt;
}

QStringList queryPlaceholders(qsizetype count)
{
    QStringList placeholders;
    placeholders.fill(QStringLiteral("?"), count);
    return placeholders;
}

void appendAggregateText(QStringList *parts,
                         qsizetype *characterCount,
                         const QVariant &value)
{
    if (*characterCount >= MaxAggregateCharacters) {
        return;
    }
    auto text = value.toString().simplified();
    if (text.isEmpty()) {
        return;
    }
    const auto remaining = MaxAggregateCharacters - *characterCount;
    if (text.size() > remaining) {
        text.truncate(remaining);
    }
    parts->append(text);
    *characterCount += text.size();
}

struct AssetDocumentState {
    SearchDocumentInput input;
    QStringList parts;
    qsizetype characterCount = 0;
};

bool collectFolderDocumentsForKeys(QSqlDatabase db,
                                   const QStringList &folderKeys,
                                   const SearchDocumentBatchConsumer &consumer,
                                   QString *errorMessage)
{
    for (qsizetype offset = 0; offset < folderKeys.size(); offset += SearchDocumentBatchSize) {
        const auto pageSize = qMin(SearchDocumentBatchSize, folderKeys.size() - offset);
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT folder_key, project_name, source_root_name, name, absolute_path, "
            "relative_path, parent_relative_path, normalized_date, date_anchor, updated_at "
            "FROM global_folder_node WHERE folder_key IN (%1) ORDER BY folder_key")
                          .arg(queryPlaceholders(pageSize).join(QLatin1Char(','))));
        for (qsizetype index = 0; index < pageSize; ++index) {
            query.addBindValue(folderKeys.at(offset + index));
        }
        if (!executeQuery(&query, QStringLiteral("读取增量文件夹搜索文档"), errorMessage)) {
            return false;
        }
        QVector<SearchDocumentInput> documents;
        documents.reserve(pageSize);
        while (query.next()) {
            SearchDocumentInput input;
            input.entityKey = query.value(0).toString();
            if (input.entityKey.trimmed().isEmpty()) {
                continue;
            }
            input.documentKey = QStringLiteral("folder:%1").arg(input.entityKey);
            input.documentType = SearchDocumentType::Folder;
            QStringList parts;
            appendTexts(&parts,
                        {query.value(1), query.value(2), query.value(3), query.value(4),
                         query.value(5), query.value(6), query.value(7), query.value(8),
                         QStringLiteral("文件夹 目录")});
            parts.removeDuplicates();
            input.contentText = parts.join(QLatin1Char(' '));
            input.sourceUpdatedAt = query.value(9).toString();
            documents.append(std::move(input));
        }
        if (!documents.isEmpty() && !consumer(std::move(documents), errorMessage)) {
            return false;
        }
    }
    return true;
}

bool collectAssetDocumentsForKeyPage(QSqlDatabase db,
                                     const QStringList &videoKeys,
                                     const SearchDocumentBatchConsumer &consumer,
                                     QSet<QString> *existingVideoKeys,
                                     QString *errorMessage)
{
    if (videoKeys.isEmpty()) {
        return true;
    }
    const auto placeholders = queryPlaceholders(videoKeys.size()).join(QLatin1Char(','));
    QHash<QString, AssetDocumentState> assets;
    QSqlQuery assetQuery(db);
    assetQuery.prepare(QStringLiteral(
        "SELECT g.video_key, g.project_name, g.source_root_name, g.file_name, "
        "g.extension, g.absolute_path, g.relative_path, g.asset_type, "
        "g.technical_summary, g.embedded_metadata_text, g.source_text, g.modified_at, g.capture_time, g.capture_date, "
        "g.capture_time_source, g.capture_time_confidence, g.updated_at, "
        "COALESCE(r.summary, ''), COALESCE(r.keywords_json, ''), "
        "COALESCE(r.scenes_json, ''), COALESCE(r.search_text, ''), "
        "COALESCE(r.analyzed_at, ''), COALESCE(r.confirmed_at, '') "
        "FROM global_video_asset g "
        "LEFT JOIN video_analysis_result r ON r.video_key = g.video_key "
        "WHERE g.video_key IN (%1) ORDER BY g.video_key").arg(placeholders));
    for (const auto &videoKey : videoKeys) {
        assetQuery.addBindValue(videoKey);
    }
    if (!executeQuery(&assetQuery, QStringLiteral("读取增量素材搜索文档"), errorMessage)) {
        return false;
    }
    while (assetQuery.next()) {
        AssetDocumentState state;
        state.input.entityKey = assetQuery.value(0).toString();
        if (state.input.entityKey.trimmed().isEmpty()) {
            continue;
        }
        state.input.documentKey = QStringLiteral("asset:%1").arg(state.input.entityKey);
        state.input.documentType = SearchDocumentType::Asset;
        const QList<QVariant> baseParts = {
            assetQuery.value(1), assetQuery.value(2), assetQuery.value(3), assetQuery.value(4),
            assetQuery.value(5), assetQuery.value(6),
            Formatters::assetTypeLabel(static_cast<AssetType>(assetQuery.value(7).toInt())),
            assetQuery.value(8), assetQuery.value(9), assetQuery.value(10), assetQuery.value(11),
            assetQuery.value(12), assetQuery.value(13), assetQuery.value(14), assetQuery.value(17),
            assetQuery.value(18), assetQuery.value(19), assetQuery.value(20)};
        for (const auto &part : baseParts) {
            appendAggregateText(&state.parts, &state.characterCount, part);
        }
        state.input.sourceUpdatedAt = assetQuery.value(16).toString();
        updateSourceTimestamp(&state.input, assetQuery.value(21).toString());
        updateSourceTimestamp(&state.input, assetQuery.value(22).toString());
        existingVideoKeys->insert(state.input.entityKey);
        assets.insert(state.input.entityKey, std::move(state));
    }

    QVector<SearchDocumentInput> pendingDocuments;
    pendingDocuments.reserve(SearchDocumentBatchSize);
    const auto flushPending = [&]() {
        if (pendingDocuments.isEmpty()) {
            return true;
        }
        auto batch = std::move(pendingDocuments);
        pendingDocuments.clear();
        pendingDocuments.reserve(SearchDocumentBatchSize);
        return consumer(std::move(batch), errorMessage);
    };

    QSqlQuery frameQuery(db);
    frameQuery.prepare(QStringLiteral(
        "SELECT video_key, frame_number, timestamp_ms, caption, tags_json, objects_json, actions, setting_text, "
        "entities_json, ocr_text, ocr_blocks_json, analyzed_at "
        "FROM video_frame_analysis WHERE video_key IN (%1) ORDER BY video_key, frame_number")
                           .arg(placeholders));
    for (const auto &videoKey : videoKeys) {
        frameQuery.addBindValue(videoKey);
    }
    if (!executeQuery(&frameQuery, QStringLiteral("读取增量逐帧搜索文档"), errorMessage)) {
        return false;
    }
    while (frameQuery.next()) {
        const auto videoKey = frameQuery.value(0).toString();
        auto asset = assets.find(videoKey);
        if (asset == assets.end()) {
            continue;
        }
        for (int column = 3; column <= 10; ++column) {
            appendAggregateText(&asset->parts, &asset->characterCount, frameQuery.value(column));
        }
        updateSourceTimestamp(&asset->input, frameQuery.value(11).toString());

        SearchDocumentInput frameDocument;
        frameDocument.documentKey = QStringLiteral("frame:%1:%2")
                                        .arg(videoKey)
                                        .arg(frameQuery.value(1).toInt());
        frameDocument.documentType = SearchDocumentType::VisualEntity;
        frameDocument.entityKey = videoKey;
        QStringList frameParts;
        appendTexts(&frameParts,
                    {QStringLiteral("画面 帧"), frameQuery.value(3), frameQuery.value(4),
                     frameQuery.value(5), frameQuery.value(6), frameQuery.value(7),
                     frameQuery.value(8), frameQuery.value(9), frameQuery.value(10)});
        const auto timestampMs = frameQuery.value(2).toLongLong();
        if (timestampMs >= 0) {
            frameParts.append(QStringLiteral("时间点 %1 毫秒").arg(timestampMs));
        }
        frameParts.removeDuplicates();
        frameDocument.contentText = frameParts.join(QLatin1Char(' ')).left(MaxAggregateCharacters);
        frameDocument.sourceUpdatedAt = frameQuery.value(11).toString();
        if (!frameDocument.contentText.trimmed().isEmpty()) {
            pendingDocuments.append(std::move(frameDocument));
            if (pendingDocuments.size() >= SearchDocumentBatchSize && !flushPending()) {
                return false;
            }
        }
    }

    const auto appendAnalysisParts = [&](const QString &statement,
                                         const QString &context,
                                         int firstTextColumn,
                                         int lastTextColumn,
                                         int timestampColumn) {
        QSqlQuery query(db);
        query.prepare(statement.arg(placeholders));
        for (const auto &videoKey : videoKeys) {
            query.addBindValue(videoKey);
        }
        if (!executeQuery(&query, context, errorMessage)) {
            return false;
        }
        while (query.next()) {
            auto asset = assets.find(query.value(0).toString());
            if (asset == assets.end()) {
                continue;
            }
            for (int column = firstTextColumn; column <= lastTextColumn; ++column) {
                appendAggregateText(&asset->parts, &asset->characterCount, query.value(column));
            }
            updateSourceTimestamp(&asset->input, query.value(timestampColumn).toString());
        }
        return true;
    };
    if (!appendAnalysisParts(
            QStringLiteral("SELECT video_key, dimension_name, detail, analyzed_at "
                           "FROM material_dimension_analysis WHERE video_key IN (%1) ORDER BY video_key, id"),
            QStringLiteral("读取增量维度搜索文档"), 1, 2, 3)
        || !appendAnalysisParts(
            QStringLiteral("SELECT video_key, dimension_name, detail, analyzed_at "
                           "FROM material_dimension_frame_analysis WHERE analysis_state = 1 "
                           "AND video_key IN (%1) ORDER BY video_key, id"),
            QStringLiteral("读取增量逐帧维度搜索文档"), 1, 2, 3)) {
        return false;
    }

    for (auto asset = assets.begin(); asset != assets.end(); ++asset) {
        asset->parts.removeDuplicates();
        asset->input.contentText = asset->parts.join(QLatin1Char(' ')).left(MaxAggregateCharacters);
        pendingDocuments.append(std::move(asset->input));
        if (pendingDocuments.size() >= SearchDocumentBatchSize && !flushPending()) {
            return false;
        }
    }
    return flushPending();
}

bool collectAssetDocumentsForKeys(QSqlDatabase db,
                                  const QStringList &videoKeys,
                                  const SearchDocumentBatchConsumer &consumer,
                                  QSet<QString> *existingVideoKeys,
                                  QString *errorMessage)
{
    for (qsizetype offset = 0; offset < videoKeys.size(); offset += AssetCollectionPageSize) {
        const auto pageSize = qMin(AssetCollectionPageSize, videoKeys.size() - offset);
        if (!collectAssetDocumentsForKeyPage(
                db,
                videoKeys.sliced(offset, pageSize),
                consumer,
                existingVideoKeys,
                errorMessage)) {
            return false;
        }
    }
    return true;
}

bool synchronizeDatabase(GlobalDatabaseManager *databaseManager,
                         SemanticSearchIndexService *semanticSearchIndexService,
                         SemanticIndexUpdateResult *result,
                         QString *errorMessage,
                         const SemanticIndexProgressCallback &progressCallback = {})
{
    if (!databaseManager || !databaseManager->isOpen() || !semanticSearchIndexService) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("搜索文档同步依赖尚未就绪");
        }
        return false;
    }
    SemanticIndexUpdateResult aggregateResult;
    auto db = databaseManager->database();
    QSqlQuery temporary(db);
    if (!temporary.exec(QStringLiteral(
            "CREATE TEMP TABLE IF NOT EXISTS search_full_sync_seen ("
            "document_key TEXT PRIMARY KEY)"))
        || !temporary.exec(QStringLiteral("DELETE FROM search_full_sync_seen"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("准备全量搜索文档水位失败：%1")
                                .arg(temporary.lastError().text());
        }
        return false;
    }

    qint64 totalDocuments = 0;
    QSqlQuery total(db);
    if (total.exec(QStringLiteral(
            "SELECT (SELECT COUNT(*) FROM global_folder_node) + "
            "(SELECT COUNT(*) FROM global_video_asset) + "
            "(SELECT COUNT(*) FROM video_frame_analysis WHERE "
            "TRIM(COALESCE(caption, '') || COALESCE(tags_json, '') || COALESCE(objects_json, '') || "
            "COALESCE(actions, '') || COALESCE(setting_text, '') || COALESCE(entities_json, '') || "
            "COALESCE(ocr_text, '') || COALESCE(ocr_blocks_json, '')) != '')"))
        && total.next()) {
        totalDocuments = total.value(0).toLongLong();
    }
    qint64 processedDocuments = 0;
    bool semanticIndexTouched = false;
    const SearchDocumentBatchConsumer consumer =
        [&](QVector<SearchDocumentInput> &&documents, QString *batchError) {
            QStringList documentKeys;
            documentKeys.reserve(documents.size());
            for (const auto &document : documents) {
                documentKeys.append(document.documentKey);
            }
            SemanticIndexUpdateResult batchResult;
            if (!semanticSearchIndexService->applyChanges(
                    documents, {}, &batchResult, batchError, progressCallback)) {
                return false;
            }
            semanticIndexTouched = true;
            mergeUpdateResult(&aggregateResult, batchResult);
            QSqlQuery seen(db);
            seen.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO search_full_sync_seen(document_key) VALUES (?)"));
            for (const auto &documentKey : documentKeys) {
                seen.addBindValue(documentKey);
                if (!seen.exec()) {
                    if (batchError) {
                        *batchError = QStringLiteral("记录全量搜索文档水位失败：%1")
                                          .arg(seen.lastError().text());
                    }
                    return false;
                }
                seen.finish();
            }
            processedDocuments += documents.size();
            if (progressCallback) {
                progressCallback(processedDocuments,
                                 totalDocuments,
                                 QStringLiteral("已分页处理 %1 份搜索文档")
                                     .arg(processedDocuments));
            }
            return true;
        };

    auto lastFolderKey = QStringLiteral("");
    while (true) {
        QSqlQuery page(db);
        page.prepare(QStringLiteral(
            "SELECT folder_key FROM global_folder_node WHERE folder_key > ? "
            "ORDER BY folder_key LIMIT ?"));
        page.addBindValue(lastFolderKey);
        page.addBindValue(SearchDocumentBatchSize);
        if (!executeQuery(&page, QStringLiteral("分页读取文件夹搜索键"), errorMessage)) {
            return false;
        }
        QStringList folderKeys;
        while (page.next()) {
            folderKeys.append(page.value(0).toString());
        }
        if (folderKeys.isEmpty()) {
            break;
        }
        lastFolderKey = folderKeys.constLast();
        if (!collectFolderDocumentsForKeys(db, folderKeys, consumer, errorMessage)) {
            return false;
        }
    }

    auto lastVideoKey = QStringLiteral("");
    while (true) {
        QSqlQuery page(db);
        page.prepare(QStringLiteral(
            "SELECT video_key FROM global_video_asset WHERE video_key > ? "
            "ORDER BY video_key LIMIT ?"));
        page.addBindValue(lastVideoKey);
        page.addBindValue(AssetCollectionPageSize);
        if (!executeQuery(&page, QStringLiteral("分页读取素材搜索键"), errorMessage)) {
            return false;
        }
        QStringList videoKeys;
        while (page.next()) {
            videoKeys.append(page.value(0).toString());
        }
        if (videoKeys.isEmpty()) {
            break;
        }
        lastVideoKey = videoKeys.constLast();
        QSet<QString> existingVideoKeys;
        if (!collectAssetDocumentsForKeys(
                db, videoKeys, consumer, &existingVideoKeys, errorMessage)) {
            return false;
        }
    }

    auto lastRemovedKey = QStringLiteral("");
    while (true) {
        QSqlQuery stale(db);
        stale.prepare(QStringLiteral(
            "SELECT document_key FROM search_document WHERE document_key > ? "
            "AND document_type IN (?, ?, ?) "
            "AND NOT EXISTS (SELECT 1 FROM search_full_sync_seen seen "
            "WHERE seen.document_key = search_document.document_key) "
            "ORDER BY document_key LIMIT ?"));
        stale.addBindValue(lastRemovedKey);
        stale.addBindValue(static_cast<int>(SearchDocumentType::Folder));
        stale.addBindValue(static_cast<int>(SearchDocumentType::Asset));
        stale.addBindValue(static_cast<int>(SearchDocumentType::VisualEntity));
        stale.addBindValue(SearchDocumentBatchSize);
        if (!executeQuery(&stale, QStringLiteral("分页读取过期搜索文档"), errorMessage)) {
            return false;
        }
        QStringList removals;
        while (stale.next()) {
            removals.append(stale.value(0).toString());
        }
        if (removals.isEmpty()) {
            break;
        }
        lastRemovedKey = removals.constLast();
        SemanticIndexUpdateResult batchResult;
        if (!semanticSearchIndexService->applyChanges(
                {}, removals, &batchResult, errorMessage, progressCallback)) {
            return false;
        }
        semanticIndexTouched = true;
        mergeUpdateResult(&aggregateResult, batchResult);
    }
    if (!semanticIndexTouched) {
        SemanticIndexUpdateResult emptyResult;
        if (!semanticSearchIndexService->applyChanges(
                {}, {}, &emptyResult, errorMessage, progressCallback)) {
            return false;
        }
        mergeUpdateResult(&aggregateResult, emptyResult);
    }
    if (result) {
        *result = aggregateResult;
    }
    return true;
}

bool synchronizeIncrementalDatabase(
    GlobalDatabaseManager *databaseManager,
    SemanticSearchIndexService *semanticSearchIndexService,
    const QVector<CatalogChangeSet> &changeSets,
    const QStringList &analysisVideoKeys,
    SemanticIndexUpdateResult *result,
    QString *errorMessage,
    const SemanticIndexProgressCallback &progressCallback = {})
{
    if (!databaseManager || !databaseManager->isOpen() || !semanticSearchIndexService) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("搜索文档同步依赖尚未就绪");
        }
        return false;
    }
    SemanticIndexUpdateResult aggregateResult;
    auto db = databaseManager->database();
    QSqlQuery temporary(db);
    if (!temporary.exec(QStringLiteral(
            "CREATE TEMP TABLE IF NOT EXISTS search_incremental_seen ("
            "document_key TEXT PRIMARY KEY)"))
        || !temporary.exec(QStringLiteral("DELETE FROM search_incremental_seen"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("准备增量搜索文档水位失败：%1")
                                .arg(temporary.lastError().text());
        }
        return false;
    }

    QStringList folderKeys;
    QStringList videoKeys = analysisVideoKeys;
    QStringList removals;
    for (const auto &changeSet : changeSets) {
        for (const auto &change : changeSet.changes) {
            if (change.entity == CatalogChangeEntity::Asset) {
                if (change.changeMask == CatalogChangeMask::Thumbnail) {
                    continue;
                }
                videoKeys.append(QStringLiteral("%1:%2")
                                     .arg(changeSet.projectUuid)
                                     .arg(change.entityId));
                continue;
            }

            const auto currentFolderKey = FolderPathMetadata::globalFolderKey(
                changeSet.projectUuid, change.sourceRootId, change.entityKey);
            if (change.operation != CatalogChangeOperation::Removed) {
                folderKeys.append(currentFolderKey);
            } else {
                removals.append(QStringLiteral("folder:%1").arg(currentFolderKey));
            }
            if (!change.previousEntityKey.isEmpty() || change.previousSourceRootId != 0) {
                const auto previousFolderKey = FolderPathMetadata::globalFolderKey(
                    changeSet.projectUuid,
                    change.previousSourceRootId,
                    change.previousEntityKey);
                if (previousFolderKey != currentFolderKey) {
                    removals.append(QStringLiteral("folder:%1").arg(previousFolderKey));
                }
            }
        }
    }
    folderKeys.removeDuplicates();
    videoKeys.removeDuplicates();
    removals.removeDuplicates();
    if (folderKeys.isEmpty() && videoKeys.isEmpty() && removals.isEmpty()) {
        if (result) {
            *result = aggregateResult;
        }
        return true;
    }

    qint64 processedDocuments = 0;
    QSet<QString> seenFolderDocumentKeys;
    const SearchDocumentBatchConsumer consumer =
        [&](QVector<SearchDocumentInput> &&documents, QString *batchError) {
            QStringList documentKeys;
            documentKeys.reserve(documents.size());
            for (const auto &document : documents) {
                documentKeys.append(document.documentKey);
                if (document.documentType == SearchDocumentType::Folder) {
                    seenFolderDocumentKeys.insert(document.documentKey);
                }
            }
            SemanticIndexUpdateResult batchResult;
            if (!semanticSearchIndexService->applyChanges(
                    documents, {}, &batchResult, batchError, progressCallback)) {
                return false;
            }
            mergeUpdateResult(&aggregateResult, batchResult);
            QSqlQuery seen(db);
            seen.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO search_incremental_seen(document_key) VALUES (?)"));
            for (const auto &documentKey : documentKeys) {
                seen.addBindValue(documentKey);
                if (!seen.exec()) {
                    if (batchError) {
                        *batchError = QStringLiteral("记录增量搜索文档水位失败：%1")
                                          .arg(seen.lastError().text());
                    }
                    return false;
                }
                seen.finish();
            }
            processedDocuments += documents.size();
            if (progressCallback) {
                progressCallback(processedDocuments,
                                 folderKeys.size() + videoKeys.size(),
                                 QStringLiteral("已增量处理 %1 份搜索文档")
                                     .arg(processedDocuments));
            }
            return true;
        };

    if (!collectFolderDocumentsForKeys(db, folderKeys, consumer, errorMessage)) {
        return false;
    }
    for (const auto &folderKey : folderKeys) {
        const auto documentKey = QStringLiteral("folder:%1").arg(folderKey);
        if (!seenFolderDocumentKeys.contains(documentKey)) {
            removals.append(documentKey);
        }
    }

    QSet<QString> existingVideoKeys;
    if (!collectAssetDocumentsForKeys(
            db, videoKeys, consumer, &existingVideoKeys, errorMessage)) {
        return false;
    }
    for (const auto &videoKey : videoKeys) {
        if (!existingVideoKeys.contains(videoKey)) {
            removals.append(QStringLiteral("asset:%1").arg(videoKey));
        }
    }

    for (qsizetype offset = 0; offset < videoKeys.size(); offset += SearchDocumentBatchSize) {
        const auto pageSize = qMin(SearchDocumentBatchSize, videoKeys.size() - offset);
        auto lastStaleFrameKey = QStringLiteral("");
        while (true) {
            QSqlQuery staleFrames(db);
            staleFrames.prepare(QStringLiteral(
                "SELECT document_key FROM search_document WHERE document_type = ? "
                "AND entity_key IN (%1) AND document_key > ? "
                "AND NOT EXISTS (SELECT 1 FROM search_incremental_seen seen "
                "WHERE seen.document_key = search_document.document_key) "
                "ORDER BY document_key LIMIT ?")
                                    .arg(queryPlaceholders(pageSize).join(QLatin1Char(','))));
            staleFrames.addBindValue(static_cast<int>(SearchDocumentType::VisualEntity));
            for (qsizetype index = 0; index < pageSize; ++index) {
                staleFrames.addBindValue(videoKeys.at(offset + index));
            }
            staleFrames.addBindValue(lastStaleFrameKey);
            staleFrames.addBindValue(SearchDocumentBatchSize);
            if (!executeQuery(&staleFrames,
                              QStringLiteral("分页读取待删除逐帧搜索文档"),
                              errorMessage)) {
                return false;
            }
            QStringList frameRemovals;
            while (staleFrames.next()) {
                frameRemovals.append(staleFrames.value(0).toString());
            }
            if (frameRemovals.isEmpty()) {
                break;
            }
            lastStaleFrameKey = frameRemovals.constLast();
            SemanticIndexUpdateResult batchResult;
            if (!semanticSearchIndexService->applyChanges(
                    {}, frameRemovals, &batchResult, errorMessage, progressCallback)) {
                return false;
            }
            mergeUpdateResult(&aggregateResult, batchResult);
        }
    }
    removals.removeDuplicates();
    for (qsizetype offset = 0; offset < removals.size(); offset += SearchDocumentBatchSize) {
        const auto pageSize = qMin(SearchDocumentBatchSize, removals.size() - offset);
        SemanticIndexUpdateResult batchResult;
        if (!semanticSearchIndexService->applyChanges(
                {},
                removals.sliced(offset, pageSize),
                &batchResult,
                errorMessage,
                progressCallback)) {
            return false;
        }
        mergeUpdateResult(&aggregateResult, batchResult);
    }
    if (result) {
        *result = aggregateResult;
    }
    return true;
}
}

SearchDocumentSyncService::SearchDocumentSyncService(
    GlobalDatabaseManager *globalDatabaseManager,
    SemanticSearchIndexService *semanticSearchIndexService,
    QObject *parent)
    : QObject(parent)
    , m_globalDatabaseManager(globalDatabaseManager)
    , m_semanticSearchIndexService(semanticSearchIndexService)
    , m_debounceTimer(new QTimer(this))
{
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(2500);
    connect(m_debounceTimer, &QTimer::timeout,
            this, &SearchDocumentSyncService::startScheduledSync);
}

SearchDocumentSyncService::~SearchDocumentSyncService()
{
    waitForIdle();
}

bool SearchDocumentSyncService::synchronizeNow(SemanticIndexUpdateResult *result,
                                               QString *errorMessage)
{
    return synchronizeDatabase(m_globalDatabaseManager,
                               m_semanticSearchIndexService,
                               result,
                               errorMessage);
}

bool SearchDocumentSyncService::synchronizeChangesNow(const CatalogChangeSet &changeSet,
                                                      SemanticIndexUpdateResult *result,
                                                      QString *errorMessage)
{
    if (changeSet.fullRebuild) {
        return synchronizeNow(result, errorMessage);
    }
    return synchronizeIncrementalDatabase(m_globalDatabaseManager,
                                          m_semanticSearchIndexService,
                                          {changeSet},
                                          {},
                                          result,
                                          errorMessage);
}

void SearchDocumentSyncService::setWorkCoordinator(IndexingWorkCoordinator *workCoordinator)
{
    m_workCoordinator = workCoordinator;
}

void SearchDocumentSyncService::waitForIdle()
{
    m_futures.waitForFinished();
}

void SearchDocumentSyncService::scheduleFullSync()
{
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()
        || !m_semanticSearchIndexService) {
        return;
    }
    m_pendingFullSync = true;
    m_pendingCatalogChanges.clear();
    m_pendingAnalysisVideoKeys.clear();
    schedulePendingWork();
}

void SearchDocumentSyncService::scheduleCatalogChanges(const CatalogChangeSet &changeSet)
{
    if (!m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()
        || !m_semanticSearchIndexService) {
        return;
    }
    if (changeSet.fullRebuild) {
        scheduleFullSync();
        return;
    }
    if (m_pendingFullSync) {
        return;
    }
    bool acceptedChange = false;
    for (const auto &change : changeSet.changes) {
        if (change.entity == CatalogChangeEntity::Asset
            && change.changeMask == CatalogChangeMask::Thumbnail) {
            continue;
        }
        acceptedChange = true;
        const auto pendingKey = QStringLiteral("%1|%2|%3")
                                    .arg(changeSet.projectUuid)
                                    .arg(static_cast<int>(change.entity))
                                    .arg(change.entityId);
        auto existing = m_pendingCatalogChanges.find(pendingKey);
        if (existing == m_pendingCatalogChanges.end()) {
            m_pendingCatalogChanges.insert(pendingKey, {changeSet.projectUuid, change});
        } else {
            auto merged = change;
            merged.changeMask |= existing->change.changeMask;
            if (existing->change.operation == CatalogChangeOperation::Added
                && change.operation == CatalogChangeOperation::Updated) {
                merged.operation = CatalogChangeOperation::Added;
            } else if (existing->change.operation == CatalogChangeOperation::Removed
                       && change.operation == CatalogChangeOperation::Added) {
                merged.operation = CatalogChangeOperation::Updated;
            }
            if (existing->change.previousEntityKey.isEmpty()) {
                merged.previousEntityKey = change.previousEntityKey;
                merged.previousSourceRootId = change.previousSourceRootId;
            } else {
                merged.previousEntityKey = existing->change.previousEntityKey;
                merged.previousSourceRootId = existing->change.previousSourceRootId;
            }
            existing->change = std::move(merged);
        }
        if (m_pendingCatalogChanges.size() > MaxPendingSearchChanges) {
            m_pendingCatalogChanges.clear();
            m_pendingAnalysisVideoKeys.clear();
            m_pendingFullSync = true;
            break;
        }
    }
    if (acceptedChange) {
        schedulePendingWork();
    }
}

void SearchDocumentSyncService::scheduleAssetSync(const QString &videoKey)
{
    const auto normalizedKey = videoKey.trimmed();
    if (normalizedKey.isEmpty() || m_pendingFullSync
        || !m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()
        || !m_semanticSearchIndexService) {
        return;
    }
    m_pendingAnalysisVideoKeys.insert(normalizedKey);
    if (m_pendingAnalysisVideoKeys.size() + m_pendingCatalogChanges.size()
        > MaxPendingSearchChanges) {
        m_pendingAnalysisVideoKeys.clear();
        m_pendingCatalogChanges.clear();
        m_pendingFullSync = true;
    }
    schedulePendingWork();
}

void SearchDocumentSyncService::schedulePendingWork()
{
    if (m_running) {
        return;
    }
    if (!m_debounceWindow.isValid()) {
        m_debounceWindow.start();
    }
    const auto remainingWindowMs = qMax<qint64>(0, 5000 - m_debounceWindow.elapsed());
    m_debounceTimer->start(static_cast<int>(qMin<qint64>(2500, remainingWindowMs)));
}

void SearchDocumentSyncService::startScheduledSync()
{
    if (m_running || !m_globalDatabaseManager || !m_globalDatabaseManager->isOpen()
        || !m_semanticSearchIndexService) {
        return;
    }
    m_debounceWindow.invalidate();
    const bool fullSync = m_pendingFullSync;
    QVector<CatalogChangeSet> changeSets;
    if (!fullSync) {
        QHash<QString, CatalogChangeSet> changesByProject;
        for (const auto &pending : std::as_const(m_pendingCatalogChanges)) {
            auto &changeSet = changesByProject[pending.projectUuid];
            changeSet.projectUuid = pending.projectUuid;
            changeSet.changes.append(pending.change);
        }
        changeSets = changesByProject.values();
    }
    const auto analysisVideoKeys = fullSync
        ? QStringList()
        : QStringList(m_pendingAnalysisVideoKeys.cbegin(), m_pendingAnalysisVideoKeys.cend());
    m_pendingFullSync = false;
    m_pendingCatalogChanges.clear();
    m_pendingAnalysisVideoKeys.clear();
    if (!fullSync && changeSets.isEmpty() && analysisVideoKeys.isEmpty()) {
        return;
    }
    m_running = true;
    emit synchronizationProgress(
        0,
        0,
        fullSync
            ? QStringLiteral("正在分页收集全部语义索引文档")
            : QStringLiteral("正在合并增量语义索引文档"));
    const auto indexPath = m_semanticSearchIndexService->indexFilePath();
    auto *workCoordinator = m_workCoordinator;
    const auto workGeneration = workCoordinator
        ? workCoordinator->currentGeneration()
        : quint64{0};
    QPointer<SearchDocumentSyncService> guard(this);

    auto future = QtConcurrent::run([
        guard,
        indexPath,
        workCoordinator,
        workGeneration,
        fullSync,
        changeSets,
        analysisVideoKeys]() {
        SemanticIndexUpdateResult updateResult;
        QString errorMessage;
        bool success = false;
        IndexingWorkCoordinator::Lease heavyIoLease;
        if (workCoordinator) {
            heavyIoLease = workCoordinator->acquire({
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Background,
                true,
                workGeneration});
            if (!heavyIoLease) {
                errorMessage = QStringLiteral("搜索文档同步因项目切换、队列拥塞或应用退出而取消");
            }
        }
        IndexingWorkCoordinator::Lease writerLease;
        if (errorMessage.isEmpty() && workCoordinator) {
            writerLease = workCoordinator->acquire({
                IndexingWorkCoordinator::Resource::SqliteWriter,
                IndexingWorkCoordinator::Priority::Background,
                false,
                workGeneration});
            if (!writerLease) {
                errorMessage = QStringLiteral("搜索索引写入因项目切换、队列拥塞或应用退出而取消");
            }
        }
        if (errorMessage.isEmpty()) {
            GlobalDatabaseManager workerDatabaseManager;
            if (workerDatabaseManager.openDatabase(&errorMessage)) {
                {
                    SemanticSearchIndexService workerIndexService(&workerDatabaseManager, indexPath);
                    const SemanticIndexProgressCallback progressCallback =
                        [guard](qsizetype processed, qsizetype total, const QString &detail) {
                            if (!guard) {
                                return;
                            }
                            QMetaObject::invokeMethod(guard, [guard, processed, total, detail]() {
                                if (!guard) {
                                    return;
                                }
                                emit guard->synchronizationProgress(
                                    static_cast<int>(qMin<qsizetype>(processed, std::numeric_limits<int>::max())),
                                    static_cast<int>(qMin<qsizetype>(total, std::numeric_limits<int>::max())),
                                    detail);
                            }, Qt::QueuedConnection);
                        };
                    success = fullSync
                        ? synchronizeDatabase(&workerDatabaseManager,
                                              &workerIndexService,
                                              &updateResult,
                                              &errorMessage,
                                              progressCallback)
                        : synchronizeIncrementalDatabase(&workerDatabaseManager,
                                                         &workerIndexService,
                                                         changeSets,
                                                         analysisVideoKeys,
                                                         &updateResult,
                                                         &errorMessage,
                                                         progressCallback);
                }
                workerDatabaseManager.closeDatabase();
            }
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard, [guard, success, updateResult, errorMessage]() {
            if (!guard) {
                return;
            }
            if (success && guard->m_semanticSearchIndexService) {
                guard->m_semanticSearchIndexService->discardLoadedIndex();
            }
            guard->m_running = false;
            const auto message = success
                ? QStringLiteral("搜索文档同步完成")
                : errorMessage;
            emit guard->synchronizationFinished(success,
                                                updateResult.inserted,
                                                updateResult.updated,
                                                updateResult.unchanged,
                                                updateResult.removed,
                                                message);
            if (guard->m_pendingFullSync
                || !guard->m_pendingCatalogChanges.isEmpty()
                || !guard->m_pendingAnalysisVideoKeys.isEmpty()) {
                guard->schedulePendingWork();
            }
        }, Qt::QueuedConnection);
    });
    m_futures.addFuture(future);
}
