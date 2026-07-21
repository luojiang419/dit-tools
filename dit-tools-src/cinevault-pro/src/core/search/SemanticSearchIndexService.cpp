#include "core/search/SemanticSearchIndexService.h"

#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"
#include "shared/SearchConfiguration.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <mutex>
#include <utility>

namespace {
QMutex processSemanticIndexMutex;
QMutex processSemanticUpdateMutex;

struct ExistingDocument {
    qint64 id = 0;
    SearchDocumentType documentType = SearchDocumentType::Unknown;
    QString entityKey;
    QString contentHash;
    QString contentText;
    QString sourceUpdatedAt;
    QString modelId;
    int indexSchemaVersion = 0;
    QString indexedAt;
};

struct PendingDocument {
    SearchDocumentInput input;
    QString contentHash;
    ExistingDocument existing;
    QVector<float> embedding;
    bool exists = false;
    bool contentChanged = false;
};

QString currentModelId()
{
    const auto id = cinevault::searchconfig::kEmbeddingModelId;
    return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

QString normalizedDocumentText(QString text)
{
    text = text.normalized(QString::NormalizationForm_C).trimmed();
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text;
}

QString contentHash(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString currentTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool validateDatabase(GlobalDatabaseManager *manager, QString *errorMessage)
{
    if (manager && manager->isOpen()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("全局素材数据库尚未打开");
    }
    return false;
}
}

SemanticSearchIndexService::SemanticSearchIndexService(GlobalDatabaseManager *globalDatabaseManager,
                                                       QString indexFilePath)
    : m_globalDatabaseManager(globalDatabaseManager)
    , m_indexFilePath(indexFilePath.trimmed().isEmpty()
                          ? Paths::semanticSearchIndexPath()
                          : QDir::cleanPath(indexFilePath))
{
}

bool SemanticSearchIndexService::ensureReady(QString *errorMessage)
{
    QMutexLocker updateLocker(&processSemanticUpdateMutex);
    QMutexLocker processLocker(&processSemanticIndexMutex);
    QMutexLocker locker(&m_mutex);
    m_lastEnsureRebuilt = false;
    return ensureReadyLocked(errorMessage, true);
}

bool SemanticSearchIndexService::rebuild(QString *errorMessage)
{
    QMutexLocker updateLocker(&processSemanticUpdateMutex);
    QMutexLocker processLocker(&processSemanticIndexMutex);
    QMutexLocker locker(&m_mutex);
    m_ready = false;
    m_lastEnsureRebuilt = false;
    return rebuildLocked(QStringLiteral("用户请求重建语义索引"), errorMessage);
}

bool SemanticSearchIndexService::invalidate(const QString &reason, QString *errorMessage)
{
    QMutexLocker updateLocker(&processSemanticUpdateMutex);
    QMutexLocker processLocker(&processSemanticIndexMutex);
    QMutexLocker locker(&m_mutex);
    if (!validateDatabase(m_globalDatabaseManager, errorMessage)) {
        return false;
    }
    m_ready = false;
    m_index = SemanticVectorIndex();
    return setStateLocked(QStringLiteral("dirty"), reason.trimmed(), errorMessage);
}

bool SemanticSearchIndexService::ensureReadyLocked(QString *errorMessage, bool allowRebuild)
{
    if (m_ready) {
        return true;
    }
    if (!validateDatabase(m_globalDatabaseManager, errorMessage)) {
        return false;
    }

    QString modelError;
    if (!m_embeddingModel.isAvailable(&modelError)) {
        const auto message = QStringLiteral("本地 BGE 模型不可用：%1").arg(modelError);
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }

    auto db = m_globalDatabaseManager->database();
    QSqlQuery stateQuery(db);
    if (!stateQuery.exec(QStringLiteral(
            "SELECT schema_version, model_id, dimensions, status, document_count "
            "FROM search_index_state WHERE singleton = 1"))
        || !stateQuery.next()) {
        const auto message = QStringLiteral("读取语义索引状态失败：%1")
                                 .arg(stateQuery.lastError().text());
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    const auto schemaVersion = stateQuery.value(0).toInt();
    const auto modelId = stateQuery.value(1).toString();
    const auto dimensions = stateQuery.value(2).toInt();
    const auto status = stateQuery.value(3).toString();
    const auto recordedDocumentCount = stateQuery.value(4).toLongLong();
    stateQuery.finish();

    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM search_document"))
        || !countQuery.next()) {
        const auto message = QStringLiteral("读取语义文档数量失败：%1")
                                 .arg(countQuery.lastError().text());
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    const auto actualDocumentCount = countQuery.value(0).toLongLong();
    countQuery.finish();

    const bool contractMatches = status == QStringLiteral("ready")
        && schemaVersion == cinevault::searchconfig::kSearchIndexSchemaVersion
        && modelId == currentModelId()
        && dimensions == cinevault::searchconfig::kEmbeddingDimensions
        && recordedDocumentCount == actualDocumentCount;
    if (contractMatches && QFileInfo::exists(m_indexFilePath)) {
        SemanticVectorIndex loadedIndex;
        QString loadError;
        if (loadedIndex.load(m_indexFilePath,
                             cinevault::searchconfig::kEmbeddingDimensions,
                             &loadError)) {
            const auto loadedSize = loadedIndex.size(&loadError);
            if (loadError.isEmpty() && loadedSize == actualDocumentCount) {
                m_index = std::move(loadedIndex);
                m_ready = true;
                return true;
            }
        }
        if (!allowRebuild) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("语义索引需要后台重建，当前已使用关键词检索");
            }
            return false;
        }
        return rebuildLocked(QStringLiteral("USearch 持久索引损坏或文档数量不一致：%1")
                                 .arg(loadError),
                             errorMessage);
    }

    QString reason;
    if (status != QStringLiteral("ready")) {
        reason = QStringLiteral("语义索引状态为 %1").arg(status);
    } else if (schemaVersion != cinevault::searchconfig::kSearchIndexSchemaVersion) {
        reason = QStringLiteral("语义索引结构版本已变化");
    } else if (modelId != currentModelId()) {
        reason = QStringLiteral("语义模型版本已变化");
    } else if (dimensions != cinevault::searchconfig::kEmbeddingDimensions) {
        reason = QStringLiteral("语义向量维度已变化");
    } else if (recordedDocumentCount != actualDocumentCount) {
        reason = QStringLiteral("语义文档数量已变化");
    } else {
        reason = QStringLiteral("USearch 持久索引文件缺失");
    }
    if (!allowRebuild) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("语义索引正在后台准备：%1；当前已使用关键词检索")
                                .arg(reason);
        }
        return false;
    }
    return rebuildLocked(reason, errorMessage);
}

bool SemanticSearchIndexService::rebuildLocked(const QString &reason, QString *errorMessage)
{
    if (!validateDatabase(m_globalDatabaseManager, errorMessage)) {
        return false;
    }
    QString modelError;
    if (!m_embeddingModel.isAvailable(&modelError)) {
        const auto message = QStringLiteral("本地 BGE 模型不可用：%1").arg(modelError);
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    if (!setStateLocked(QStringLiteral("dirty"), reason, errorMessage)) {
        return false;
    }

    auto db = m_globalDatabaseManager->database();
    qint64 validDocumentCount = 0;
    QSqlQuery count(db);
    if (!count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM search_document "
            "WHERE id > 0 AND TRIM(COALESCE(content_text, '')) != ''"))
        || !count.next()) {
        const auto message = QStringLiteral("读取语义文档数量失败：%1")
                                 .arg(count.lastError().text());
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    validDocumentCount = count.value(0).toLongLong();
    count.finish();

    SemanticVectorIndex rebuiltIndex;
    QString indexError;
    if (!rebuiltIndex.reset(cinevault::searchconfig::kEmbeddingDimensions, &indexError)
        || !rebuiltIndex.reserve(static_cast<qsizetype>(validDocumentCount), &indexError)) {
        const auto message = QStringLiteral("创建 USearch 重建索引失败：%1").arg(indexError);
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }

    if (!ensureIndexDirectoryLocked(errorMessage)) {
        return false;
    }
    if (!db.transaction()) {
        const auto message = QStringLiteral("无法开始语义索引重建事务：%1").arg(db.lastError().text());
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    const auto rollbackFailure = [&](const QString &message) {
        db.rollback();
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    };

    QSqlQuery deleteEmpty(db);
    if (!deleteEmpty.exec(QStringLiteral(
            "DELETE FROM search_document WHERE id <= 0 "
            "OR TRIM(COALESCE(content_text, '')) = ''"))) {
        return rollbackFailure(QStringLiteral("清理空语义文档失败：%1")
                                   .arg(deleteEmpty.lastError().text()));
    }

    const auto timestamp = currentTimestamp();
    QSqlQuery updateDocument(db);
    updateDocument.prepare(QStringLiteral(
        "UPDATE search_document SET content_hash = ?, content_text = ?, model_id = ?, "
        "index_schema_version = ?, indexed_at = ? WHERE id = ?"));
    QSqlQuery deleteNormalizedEmpty(db);
    deleteNormalizedEmpty.prepare(QStringLiteral("DELETE FROM search_document WHERE id = ?"));

    struct RebuildDocument {
        qint64 id = 0;
        QString contentText;
        QString contentHash;
    };
    constexpr qsizetype RebuildPageSize = 256;
    qint64 lastDocumentId = 0;
    while (true) {
        QSqlQuery page(db);
        page.prepare(QStringLiteral(
            "SELECT id, content_text FROM search_document WHERE id > ? "
            "ORDER BY id LIMIT ?"));
        page.addBindValue(lastDocumentId);
        page.addBindValue(RebuildPageSize);
        if (!page.exec()) {
            return rollbackFailure(QStringLiteral("分页读取语义文档失败：%1")
                                       .arg(page.lastError().text()));
        }
        QVector<RebuildDocument> documents;
        documents.reserve(RebuildPageSize);
        QStringList documentTexts;
        documentTexts.reserve(RebuildPageSize);
        QVector<qint64> normalizedEmptyIds;
        bool hadRows = false;
        while (page.next()) {
            hadRows = true;
            RebuildDocument document;
            document.id = page.value(0).toLongLong();
            document.contentText = normalizedDocumentText(page.value(1).toString());
            document.contentHash = contentHash(document.contentText);
            lastDocumentId = document.id;
            if (document.id <= 0 || document.contentText.isEmpty()) {
                normalizedEmptyIds.append(document.id);
                continue;
            }
            documentTexts.append(document.contentText);
            documents.append(std::move(document));
        }
        page.finish();
        for (const auto documentId : std::as_const(normalizedEmptyIds)) {
            deleteNormalizedEmpty.bindValue(0, documentId);
            if (!deleteNormalizedEmpty.exec()) {
                return rollbackFailure(QStringLiteral("清理规范化后空语义文档失败：%1")
                                           .arg(deleteNormalizedEmpty.lastError().text()));
            }
            deleteNormalizedEmpty.finish();
        }
        if (!hadRows) {
            break;
        }
        if (documents.isEmpty()) {
            continue;
        }

        const auto embeddings = m_embeddingModel.embedDocuments(documentTexts, {}, &modelError);
        if (embeddings.size() != documents.size()) {
            return rollbackFailure(QStringLiteral("批量生成语义文档向量失败：%1")
                                       .arg(modelError));
        }
        for (qsizetype index = 0; index < documents.size(); ++index) {
            const auto &document = documents.at(index);
            if (!rebuiltIndex.add(
                    static_cast<quint64>(document.id), embeddings.at(index), &indexError)) {
                return rollbackFailure(QStringLiteral("写入 USearch 重建索引失败：%1")
                                           .arg(indexError));
            }
            updateDocument.bindValue(0, document.contentHash);
            updateDocument.bindValue(1, document.contentText);
            updateDocument.bindValue(2, currentModelId());
            updateDocument.bindValue(3, cinevault::searchconfig::kSearchIndexSchemaVersion);
            updateDocument.bindValue(4, timestamp);
            updateDocument.bindValue(5, document.id);
            if (!updateDocument.exec()) {
                return rollbackFailure(QStringLiteral("更新语义文档映射失败：%1")
                                           .arg(updateDocument.lastError().text()));
            }
            updateDocument.finish();
        }
    }

    if (!rebuiltIndex.save(m_indexFilePath, &indexError)) {
        return rollbackFailure(QStringLiteral("保存 USearch 重建索引失败：%1").arg(indexError));
    }

    QSqlQuery readyState(db);
    readyState.prepare(QStringLiteral(
        "UPDATE search_index_state SET schema_version = ?, model_id = ?, dimensions = ?, "
        "generation = generation + 1, status = 'ready', "
        "document_count = (SELECT COUNT(*) FROM search_document), updated_at = ?, last_error = '' "
        "WHERE singleton = 1"));
    readyState.addBindValue(cinevault::searchconfig::kSearchIndexSchemaVersion);
    readyState.addBindValue(currentModelId());
    readyState.addBindValue(cinevault::searchconfig::kEmbeddingDimensions);
    readyState.addBindValue(timestamp);
    if (!readyState.exec()) {
        return rollbackFailure(QStringLiteral("提交语义索引状态失败：%1")
                                   .arg(readyState.lastError().text()));
    }
    if (!db.commit()) {
        return rollbackFailure(QStringLiteral("提交语义索引重建事务失败：%1")
                                   .arg(db.lastError().text()));
    }

    m_index = std::move(rebuiltIndex);
    m_ready = true;
    m_lastEnsureRebuilt = true;
    return true;
}

bool SemanticSearchIndexService::applyChanges(const QVector<SearchDocumentInput> &upserts,
                                              const QStringList &removedDocumentKeys,
                                              SemanticIndexUpdateResult *result,
                                              QString *errorMessage,
                                              const SemanticIndexProgressCallback &progressCallback)
{
    QMutexLocker updateLocker(&processSemanticUpdateMutex);
    SemanticIndexUpdateResult localResult;
    m_lastEnsureRebuilt = false;
    {
        QMutexLocker processLocker(&processSemanticIndexMutex);
        QMutexLocker locker(&m_mutex);
        if (!ensureReadyLocked(errorMessage, true)) {
            return false;
        }
    }
    localResult.rebuilt = m_lastEnsureRebuilt;

    QMap<QString, SearchDocumentInput> normalizedUpserts;
    for (auto input : upserts) {
        input.documentKey = input.documentKey.trimmed();
        input.entityKey = input.entityKey.trimmed();
        input.contentText = normalizedDocumentText(input.contentText);
        input.sourceUpdatedAt = input.sourceUpdatedAt.trimmed();
        if (input.entityKey.isNull()) input.entityKey = QStringLiteral("");
        if (input.sourceUpdatedAt.isNull()) input.sourceUpdatedAt = QStringLiteral("");
        if (input.documentKey.isEmpty() || input.contentText.isEmpty()
            || input.documentType == SearchDocumentType::Unknown) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("语义文档必须包含文档键、类型和非空文本");
            }
            return false;
        }
        normalizedUpserts.insert(input.documentKey, std::move(input));
    }

    QSet<QString> normalizedRemovals;
    for (const auto &key : removedDocumentKeys) {
        const auto normalized = key.trimmed();
        if (!normalized.isEmpty() && !normalizedUpserts.contains(normalized)) {
            normalizedRemovals.insert(normalized);
        }
    }

    auto db = m_globalDatabaseManager->database();
    QHash<QString, ExistingDocument> existingDocuments;
    QStringList requestedDocumentKeys = normalizedUpserts.keys();
    for (const auto &key : std::as_const(normalizedRemovals)) {
        if (!normalizedUpserts.contains(key)) {
            requestedDocumentKeys.append(key);
        }
    }
    constexpr qsizetype ExistingDocumentPageSize = 500;
    for (qsizetype offset = 0; offset < requestedDocumentKeys.size(); offset += ExistingDocumentPageSize) {
        const auto pageSize = qMin(ExistingDocumentPageSize, requestedDocumentKeys.size() - offset);
        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), pageSize);
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
                "SELECT id, document_key, document_type, entity_key, content_hash, content_text, "
                "source_updated_at, model_id, index_schema_version, indexed_at FROM search_document "
                "WHERE document_key IN (%1)").arg(placeholders.join(QLatin1Char(','))));
        for (qsizetype index = 0; index < pageSize; ++index) {
            query.addBindValue(requestedDocumentKeys.at(offset + index));
        }
        if (!query.exec()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("读取现有语义文档失败：%1").arg(query.lastError().text());
            }
            return false;
        }
        while (query.next()) {
            ExistingDocument document;
            document.id = query.value(0).toLongLong();
            const auto key = query.value(1).toString();
            document.documentType = static_cast<SearchDocumentType>(query.value(2).toInt());
            document.entityKey = query.value(3).toString();
            document.contentHash = query.value(4).toString();
            document.contentText = query.value(5).toString();
            document.sourceUpdatedAt = query.value(6).toString();
            document.modelId = query.value(7).toString();
            document.indexSchemaVersion = query.value(8).toInt();
            document.indexedAt = query.value(9).toString();
            existingDocuments.insert(key, std::move(document));
        }
    }

    QVector<PendingDocument> pendingDocuments;
    QVector<qsizetype> changedPendingIndexes;
    QStringList changedTexts;
    for (auto iterator = normalizedUpserts.cbegin(); iterator != normalizedUpserts.cend(); ++iterator) {
        PendingDocument pending;
        pending.input = iterator.value();
        pending.contentHash = contentHash(pending.input.contentText);
        const auto existing = existingDocuments.constFind(iterator.key());
        pending.exists = existing != existingDocuments.cend();
        if (pending.exists) {
            pending.existing = existing.value();
        }
        pending.contentChanged = !pending.exists
            || pending.existing.contentHash != pending.contentHash
            || pending.existing.modelId != currentModelId()
            || pending.existing.indexSchemaVersion != cinevault::searchconfig::kSearchIndexSchemaVersion;
        const bool metadataChanged = !pending.exists
            || pending.existing.documentType != pending.input.documentType
            || pending.existing.entityKey != pending.input.entityKey
            || pending.existing.sourceUpdatedAt != pending.input.sourceUpdatedAt
            || pending.existing.contentText != pending.input.contentText;
        if (!pending.contentChanged && !metadataChanged) {
            ++localResult.unchanged;
            continue;
        }
        if (pending.exists) {
            ++localResult.updated;
        } else {
            ++localResult.inserted;
        }
        pendingDocuments.append(std::move(pending));
        if (pendingDocuments.constLast().contentChanged) {
            changedPendingIndexes.append(pendingDocuments.size() - 1);
            changedTexts.append(pendingDocuments.constLast().input.contentText);
        }
    }

    for (const auto &key : std::as_const(normalizedRemovals)) {
        if (existingDocuments.contains(key)) {
            ++localResult.removed;
        }
    }
    if (localResult.inserted == 0 && localResult.updated == 0 && localResult.removed == 0) {
        if (progressCallback) {
            progressCallback(0, 0, QStringLiteral("语义索引已是最新"));
        }
        if (result) *result = localResult;
        return true;
    }

    if (!changedTexts.isEmpty()) {
        if (progressCallback) {
            progressCallback(0,
                             changedTexts.size(),
                             QStringLiteral("正在使用 %1 个线程、每批 %2 条生成语义向量")
                                 .arg(m_embeddingModel.inferenceThreadCount())
                                 .arg(m_embeddingModel.batchSize()));
        }
        QString modelError;
        const auto embeddings = m_embeddingModel.embedDocuments(
            changedTexts,
            [progressCallback](qsizetype processed, qsizetype total) {
                if (progressCallback) {
                    progressCallback(processed,
                                     total,
                                     QStringLiteral("正在批量生成语义向量"));
                }
            },
            &modelError);
        if (embeddings.size() != changedTexts.size()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("批量生成增量语义向量失败：%1").arg(modelError);
            }
            return false;
        }
        for (qsizetype index = 0; index < changedPendingIndexes.size(); ++index) {
            pendingDocuments[changedPendingIndexes.at(index)].embedding = embeddings.at(index);
        }
    }

    if (progressCallback) {
        progressCallback(changedTexts.size(),
                         changedTexts.size(),
                         QStringLiteral("正在写入并切换语义索引"));
    }

    QMutexLocker processLocker(&processSemanticIndexMutex);
    QMutexLocker locker(&m_mutex);
    if (!setStateLocked(QStringLiteral("dirty"), QString(), errorMessage)) {
        return false;
    }
    QString indexError;
    const auto currentIndexSize = m_index.size(&indexError);
    if (!indexError.isEmpty()
        || !m_index.reserve(currentIndexSize + localResult.inserted, &indexError)) {
        const auto message = QStringLiteral("扩容增量 USearch 索引失败：%1").arg(indexError);
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    if (!ensureIndexDirectoryLocked(errorMessage)) {
        const auto message = errorMessage && !errorMessage->isEmpty()
            ? *errorMessage
            : QStringLiteral("无法准备语义索引目录");
        recordFailureLocked(message);
        return false;
    }
    if (!db.transaction()) {
        const auto message = QStringLiteral("无法开始语义索引增量事务：%1")
                                 .arg(db.lastError().text());
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    }
    const auto rollbackFailure = [&](const QString &message) {
        db.rollback();
        recordFailureLocked(message);
        if (errorMessage) *errorMessage = message;
        return false;
    };

    QSqlQuery deleteDocument(db);
    deleteDocument.prepare(QStringLiteral("DELETE FROM search_document WHERE id = ?"));
    for (const auto &key : std::as_const(normalizedRemovals)) {
        const auto existing = existingDocuments.constFind(key);
        if (existing == existingDocuments.cend()) {
            continue;
        }
        if (!m_index.remove(static_cast<quint64>(existing->id), &indexError)) {
            return rollbackFailure(QStringLiteral("删除增量语义向量失败：%1").arg(indexError));
        }
        deleteDocument.bindValue(0, existing->id);
        if (!deleteDocument.exec()) {
            return rollbackFailure(QStringLiteral("删除语义文档映射失败：%1")
                                       .arg(deleteDocument.lastError().text()));
        }
        deleteDocument.finish();
    }

    const auto timestamp = currentTimestamp();
    QSqlQuery insertDocument(db);
    insertDocument.prepare(QStringLiteral(
        "INSERT INTO search_document "
        "(document_key, document_type, entity_key, content_hash, content_text, source_updated_at, "
        "model_id, index_schema_version, indexed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    QSqlQuery updateDocument(db);
    updateDocument.prepare(QStringLiteral(
        "UPDATE search_document SET document_type = ?, entity_key = ?, content_hash = ?, "
        "content_text = ?, source_updated_at = ?, model_id = ?, index_schema_version = ?, indexed_at = ? "
        "WHERE id = ?"));

    for (const auto &pending : std::as_const(pendingDocuments)) {
        qint64 documentId = pending.existing.id;
        const auto indexedAt = pending.contentChanged ? timestamp : pending.existing.indexedAt;
        if (!pending.exists) {
            insertDocument.bindValue(0, pending.input.documentKey);
            insertDocument.bindValue(1, static_cast<int>(pending.input.documentType));
            insertDocument.bindValue(2, pending.input.entityKey);
            insertDocument.bindValue(3, pending.contentHash);
            insertDocument.bindValue(4, pending.input.contentText);
            insertDocument.bindValue(5, pending.input.sourceUpdatedAt);
            insertDocument.bindValue(6, currentModelId());
            insertDocument.bindValue(7, cinevault::searchconfig::kSearchIndexSchemaVersion);
            insertDocument.bindValue(8, indexedAt);
            if (!insertDocument.exec()) {
                return rollbackFailure(QStringLiteral("新增语义文档映射失败：%1")
                                           .arg(insertDocument.lastError().text()));
            }
            documentId = insertDocument.lastInsertId().toLongLong();
            insertDocument.finish();
        } else {
            updateDocument.bindValue(0, static_cast<int>(pending.input.documentType));
            updateDocument.bindValue(1, pending.input.entityKey);
            updateDocument.bindValue(2, pending.contentHash);
            updateDocument.bindValue(3, pending.input.contentText);
            updateDocument.bindValue(4, pending.input.sourceUpdatedAt);
            updateDocument.bindValue(5, currentModelId());
            updateDocument.bindValue(6, cinevault::searchconfig::kSearchIndexSchemaVersion);
            updateDocument.bindValue(7, indexedAt);
            updateDocument.bindValue(8, documentId);
            if (!updateDocument.exec()) {
                return rollbackFailure(QStringLiteral("更新语义文档映射失败：%1")
                                           .arg(updateDocument.lastError().text()));
            }
            updateDocument.finish();
        }
        if (documentId <= 0) {
            return rollbackFailure(QStringLiteral("语义文档映射未生成有效主键"));
        }
        if (pending.contentChanged) {
            if (pending.exists
                && !m_index.remove(static_cast<quint64>(documentId), &indexError)) {
                return rollbackFailure(QStringLiteral("替换旧语义向量失败：%1").arg(indexError));
            }
            if (!m_index.add(static_cast<quint64>(documentId), pending.embedding, &indexError)) {
                return rollbackFailure(QStringLiteral("写入增量语义向量失败：%1").arg(indexError));
            }
        }
    }

    if (!m_index.save(m_indexFilePath, &indexError)) {
        return rollbackFailure(QStringLiteral("保存增量 USearch 索引失败：%1").arg(indexError));
    }
    QSqlQuery readyState(db);
    readyState.prepare(QStringLiteral(
        "UPDATE search_index_state SET schema_version = ?, model_id = ?, dimensions = ?, "
        "generation = generation + 1, status = 'ready', "
        "document_count = (SELECT COUNT(*) FROM search_document), updated_at = ?, last_error = '' "
        "WHERE singleton = 1"));
    readyState.addBindValue(cinevault::searchconfig::kSearchIndexSchemaVersion);
    readyState.addBindValue(currentModelId());
    readyState.addBindValue(cinevault::searchconfig::kEmbeddingDimensions);
    readyState.addBindValue(timestamp);
    if (!readyState.exec()) {
        return rollbackFailure(QStringLiteral("更新增量语义索引状态失败：%1")
                                   .arg(readyState.lastError().text()));
    }
    if (!db.commit()) {
        return rollbackFailure(QStringLiteral("提交语义索引增量事务失败：%1")
                                   .arg(db.lastError().text()));
    }
    m_ready = true;
    if (result) *result = localResult;
    return true;
}

void SemanticSearchIndexService::discardLoadedIndex()
{
    QMutexLocker processLocker(&processSemanticIndexMutex);
    QMutexLocker locker(&m_mutex);
    m_ready = false;
    m_index = SemanticVectorIndex();
}

QVector<SemanticSearchHit> SemanticSearchIndexService::search(const QString &queryText,
                                                              qsizetype limit,
                                                              QString *errorMessage)
{
    std::unique_lock<QMutex> processLocker(processSemanticIndexMutex, std::try_to_lock);
    if (!processLocker.owns_lock()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("语义索引正在切换，当前已使用关键词检索");
        }
        return {};
    }
    std::unique_lock<QMutex> locker(m_mutex, std::try_to_lock);
    if (!locker.owns_lock()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("语义索引正忙，当前已使用关键词检索");
        }
        return {};
    }
    const auto normalizedQuery = normalizedDocumentText(queryText);
    if (normalizedQuery.isEmpty() || limit <= 0) {
        return {};
    }
    if (!ensureReadyLocked(errorMessage, false)) {
        return {};
    }

    QString modelError;
    const auto queryEmbedding = m_embeddingModel.embedQuery(normalizedQuery, &modelError);
    if (queryEmbedding.size() != cinevault::searchconfig::kEmbeddingDimensions) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("生成查询语义向量失败：%1").arg(modelError);
        }
        return {};
    }
    QString indexError;
    const auto vectorHits = m_index.search(queryEmbedding,
                                           std::min<qsizetype>(limit, 200),
                                           &indexError);
    if (!indexError.isEmpty()) {
        recordFailureLocked(indexError);
        if (errorMessage) *errorMessage = indexError;
        return {};
    }
    if (vectorHits.isEmpty()) {
        return {};
    }

    QStringList placeholders;
    placeholders.reserve(vectorHits.size());
    for (qsizetype index = 0; index < vectorHits.size(); ++index) {
        placeholders.append(QStringLiteral("?"));
    }
    auto db = m_globalDatabaseManager->database();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id, document_key FROM search_document WHERE id IN (%1)")
                      .arg(placeholders.join(QLatin1Char(','))));
    for (const auto &hit : vectorHits) {
        query.addBindValue(QVariant::fromValue<qulonglong>(hit.first));
    }
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("映射语义搜索结果失败：%1").arg(query.lastError().text());
        }
        return {};
    }
    QHash<quint64, QString> documentKeys;
    while (query.next()) {
        documentKeys.insert(query.value(0).toULongLong(), query.value(1).toString());
    }

    QVector<SemanticSearchHit> hits;
    hits.reserve(vectorHits.size());
    for (const auto &hit : vectorHits) {
        const auto key = documentKeys.constFind(hit.first);
        if (key == documentKeys.cend()) {
            continue;
        }
        hits.append(SemanticSearchHit{key.value(), hit.second});
    }
    return hits;
}

bool SemanticSearchIndexService::setStateLocked(const QString &status,
                                                const QString &lastError,
                                                QString *errorMessage)
{
    if (!validateDatabase(m_globalDatabaseManager, errorMessage)) {
        return false;
    }
    QSqlQuery query(m_globalDatabaseManager->database());
    query.prepare(QStringLiteral(
        "UPDATE search_index_state SET status = ?, updated_at = ?, last_error = ? WHERE singleton = 1"));
    query.addBindValue(status);
    query.addBindValue(currentTimestamp());
    query.addBindValue(lastError.isNull() ? QStringLiteral("") : lastError);
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("更新语义索引状态失败：%1").arg(query.lastError().text());
    }
    return false;
}

void SemanticSearchIndexService::recordFailureLocked(const QString &message)
{
    m_ready = false;
    m_index = SemanticVectorIndex();
    QString ignoredError;
    setStateLocked(QStringLiteral("error"), message, &ignoredError);
}

bool SemanticSearchIndexService::ensureIndexDirectoryLocked(QString *errorMessage) const
{
    const QFileInfo fileInfo(m_indexFilePath);
    if (QDir().mkpath(fileInfo.absolutePath())) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("无法创建语义索引目录：%1").arg(fileInfo.absolutePath());
    }
    return false;
}

bool SemanticSearchIndexService::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

QString SemanticSearchIndexService::indexFilePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_indexFilePath;
}
