#include "core/search/BgeEmbeddingModel.h"
#include "core/search/SemanticSearchIndexService.h"
#include "infrastructure/db/DatabaseMigration.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"
#include "shared/SearchConfiguration.h"

#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <cmath>
#include <future>

namespace {
QString globalDatabasePath()
{
    return QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("material-center.sqlite"));
}

void removeGlobalDatabaseFiles()
{
    const auto path = globalDatabasePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
    QFile::remove(DatabaseMigration::backupFilePath(path, 11));
}

qint64 scalarValue(QSqlDatabase db, const QString &sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql) || !query.next()) {
        return -1;
    }
    return query.value(0).toLongLong();
}

SearchDocumentInput document(const QString &key,
                             SearchDocumentType type,
                             const QString &entityKey,
                             const QString &text,
                             const QString &updatedAt)
{
    SearchDocumentInput input;
    input.documentKey = key;
    input.documentType = type;
    input.entityKey = entityKey;
    input.contentText = text;
    input.sourceUpdatedAt = updatedAt;
    return input;
}
}

class SemanticSearchIndexServiceTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        removeGlobalDatabaseFiles();
    }

    void bgeModelProducesNormalized512DVector()
    {
        BgeEmbeddingModel model;
        QString errorMessage;
        QVERIFY2(model.isAvailable(&errorMessage), qPrintable(errorMessage));
        const auto embedding = model.embedDocument(QStringLiteral("红色牛仔短裤"), &errorMessage);
        QCOMPARE(embedding.size(), cinevault::searchconfig::kEmbeddingDimensions);
        double squaredNorm = 0.0;
        for (const auto value : embedding) {
            squaredNorm += static_cast<double>(value) * value;
        }
        QVERIFY2(std::abs(std::sqrt(squaredNorm) - 1.0) < 1e-4,
                 qPrintable(QString::number(std::sqrt(squaredNorm), 'g', 12)));
    }

    void bgeModelProducesConsistentBatchedVectorsAndProgress()
    {
        BgeEmbeddingModel model;
        QString errorMessage;
        QVERIFY2(model.isAvailable(&errorMessage), qPrintable(errorMessage));
        const QStringList texts{
            QStringLiteral("红色牛仔短裤"),
            QStringLiteral("雪山山顶日出户外广告"),
            QStringLiteral("黑色皮革夹克 夜晚城市街道"),
            QStringLiteral("蓝色海水沙滩航拍")
        };
        qsizetype lastProcessed = 0;
        qsizetype lastTotal = 0;
        const auto embeddings = model.embedDocuments(
            texts,
            [&lastProcessed, &lastTotal](qsizetype processed, qsizetype total) {
                lastProcessed = processed;
                lastTotal = total;
            },
            &errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
        QCOMPARE(embeddings.size(), texts.size());
        QCOMPARE(lastProcessed, texts.size());
        QCOMPARE(lastTotal, texts.size());
        QVERIFY(model.inferenceThreadCount() >= 1);
        QVERIFY(model.batchSize() >= 1);

        const auto single = model.embedDocument(texts.first(), &errorMessage);
        QCOMPARE(single.size(), cinevault::searchconfig::kEmbeddingDimensions);
        double dotProduct = 0.0;
        for (int index = 0; index < single.size(); ++index) {
            dotProduct += static_cast<double>(single.at(index)) * embeddings.first().at(index);
        }
        QVERIFY2(dotProduct > 0.99, qPrintable(QString::number(dotProduct, 'g', 12)));
    }

    void incrementalChangesPersistAcrossServiceInstances()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("semantic.usearch"));

        GlobalDatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
        const QVector<SearchDocumentInput> initialDocuments{
            document(QStringLiteral("folder:summer"),
                     SearchDocumentType::Folder,
                     QStringLiteral("folder-summer"),
                     QStringLiteral("夏日海滩 蓝天 白云 旅行素材目录"),
                     QStringLiteral("2026-07-14T10:00:00Z")),
            document(QStringLiteral("asset:red-shorts"),
                     SearchDocumentType::Asset,
                     QStringLiteral("asset-red-shorts"),
                     QStringLiteral("人物穿着红色牛仔短裤 户外运动"),
                     QStringLiteral("2026-07-14T10:01:00Z"))
        };

        qint64 generationAfterInsert = 0;
        {
            SemanticSearchIndexService service(&manager, indexPath);
            SemanticIndexUpdateResult result;
            QVERIFY2(service.applyChanges(initialDocuments, {}, &result, &errorMessage),
                     qPrintable(errorMessage));
            QVERIFY(result.rebuilt);
            QCOMPARE(result.inserted, 2);
            QCOMPARE(result.updated, 0);
            QCOMPARE(result.removed, 0);
            QVERIFY(QFileInfo(indexPath).isFile());
            QVERIFY(QFileInfo(indexPath).size() > 0);

            const auto hits = service.search(QStringLiteral("红色牛仔短裤"), 5, &errorMessage);
            QVERIFY2(!hits.isEmpty(), qPrintable(errorMessage));
            QCOMPARE(hits.first().documentKey, QStringLiteral("asset:red-shorts"));
            generationAfterInsert = scalarValue(
                manager.database(),
                QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1"));
            QVERIFY(generationAfterInsert > 0);
        }

        {
            SemanticSearchIndexService service(&manager, indexPath);
            QVERIFY2(service.ensureReady(&errorMessage), qPrintable(errorMessage));
            QCOMPARE(scalarValue(manager.database(),
                                 QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1")),
                     generationAfterInsert);

            SemanticIndexUpdateResult unchangedResult;
            QVERIFY2(service.applyChanges(initialDocuments, {}, &unchangedResult, &errorMessage),
                     qPrintable(errorMessage));
            QCOMPARE(unchangedResult.unchanged, 2);
            QCOMPARE(unchangedResult.inserted, 0);
            QCOMPARE(unchangedResult.updated, 0);
            QCOMPARE(unchangedResult.removed, 0);

            const QVector<SearchDocumentInput> changedDocuments{
                document(QStringLiteral("asset:red-shorts"),
                         SearchDocumentType::Asset,
                         QStringLiteral("asset-red-shorts"),
                         QStringLiteral("黑色皮革夹克 夜晚城市街道"),
                         QStringLiteral("2026-07-14T11:00:00Z"))
            };
            SemanticIndexUpdateResult changedResult;
            QVERIFY2(service.applyChanges(changedDocuments,
                                          {QStringLiteral("folder:summer")},
                                          &changedResult,
                                          &errorMessage),
                     qPrintable(errorMessage));
            QCOMPARE(changedResult.updated, 1);
            QCOMPARE(changedResult.removed, 1);
            QCOMPARE(scalarValue(manager.database(), QStringLiteral("SELECT COUNT(*) FROM search_document")),
                     qint64{1});

            const auto hits = service.search(QStringLiteral("黑色皮革夹克"), 5, &errorMessage);
            QVERIFY2(!hits.isEmpty(), qPrintable(errorMessage));
            QCOMPARE(hits.first().documentKey, QStringLiteral("asset:red-shorts"));
        }
        manager.closeDatabase();
    }

    void searchKeepsUsingStableIndexWhileBatchUpdateIsPrepared()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("nonblocking.usearch"));

        GlobalDatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
        SemanticSearchIndexService searchService(&manager, indexPath);
        SemanticIndexUpdateResult initialResult;
        QVERIFY2(searchService.applyChanges(
                     {document(QStringLiteral("asset:stable"),
                               SearchDocumentType::Asset,
                               QStringLiteral("stable"),
                               QStringLiteral("稳定旧索引 历史纪录片归档"),
                               QStringLiteral("2026-07-14T10:00:00Z"))},
                     {},
                     &initialResult,
                     &errorMessage),
                 qPrintable(errorMessage));

        QSemaphore embeddingsPrepared;
        QSemaphore allowCommit;
        auto updateFuture = std::async(std::launch::async, [indexPath, &embeddingsPrepared, &allowCommit]() {
            GlobalDatabaseManager workerManager;
            QString workerError;
            if (!workerManager.openDatabase(&workerError)) {
                return qMakePair(false, workerError);
            }
            QVector<SearchDocumentInput> documents;
            documents.reserve(64);
            for (int index = 0; index < 64; ++index) {
                documents.append(document(QStringLiteral("asset:parallel-%1").arg(index),
                                          SearchDocumentType::Asset,
                                          QStringLiteral("parallel-%1").arg(index),
                                          QStringLiteral("并发更新素材 %1 城市夜景 露营 旅行 商业广告")
                                              .arg(index),
                                          QStringLiteral("2026-07-14T11:00:00Z")));
            }
            SemanticSearchIndexService updateService(&workerManager, indexPath);
            SemanticIndexUpdateResult updateResult;
            bool paused = false;
            const auto success = updateService.applyChanges(
                documents,
                {},
                &updateResult,
                &workerError,
                [&embeddingsPrepared, &allowCommit, &paused](qsizetype processed,
                                                             qsizetype,
                                                             const QString &) {
                    if (!paused && processed > 0) {
                        paused = true;
                        embeddingsPrepared.release();
                        allowCommit.acquire();
                    }
                });
            workerManager.closeDatabase();
            return qMakePair(success, workerError);
        });

        const auto reachedLockFreePhase = embeddingsPrepared.tryAcquire(1, 10000);
        QElapsedTimer searchTimer;
        searchTimer.start();
        errorMessage.clear();
        const auto stableHits = searchService.search(QStringLiteral("历史纪录片归档"),
                                                     5,
                                                     &errorMessage);
        const auto searchElapsedMs = searchTimer.elapsed();
        allowCommit.release();
        const auto updateOutcome = updateFuture.get();

        QVERIFY2(reachedLockFreePhase, "语义向量未在超时前完成批量准备");
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
        QVERIFY2(searchElapsedMs < 2000,
                 qPrintable(QStringLiteral("索引更新期间搜索耗时 %1 ms").arg(searchElapsedMs)));
        QVERIFY(std::any_of(stableHits.cbegin(), stableHits.cend(), [](const auto &hit) {
            return hit.documentKey == QStringLiteral("asset:stable");
        }));
        QVERIFY2(updateOutcome.first, qPrintable(updateOutcome.second));
        manager.closeDatabase();
    }

    void corruptedPersistentIndexRebuildsFromSqliteDocuments()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("corrupted.usearch"));

        GlobalDatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
        {
            SemanticSearchIndexService service(&manager, indexPath);
            SemanticIndexUpdateResult result;
            QVERIFY2(service.applyChanges(
                         {document(QStringLiteral("asset:ocean"),
                                   SearchDocumentType::Asset,
                                   QStringLiteral("asset-ocean"),
                                   QStringLiteral("海浪 沙滩 蓝色海水 航拍"),
                                   QStringLiteral("2026-07-14T12:00:00Z"))},
                         {},
                         &result,
                         &errorMessage),
                     qPrintable(errorMessage));
        }
        const auto generationBefore = scalarValue(
            manager.database(),
            QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1"));
        QFile corrupted(indexPath);
        QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(corrupted.write("broken-index"), qint64{12});
        corrupted.close();

        SemanticSearchIndexService recoveredService(&manager, indexPath);
        QVERIFY2(recoveredService.ensureReady(&errorMessage), qPrintable(errorMessage));
        const auto generationAfter = scalarValue(
            manager.database(),
            QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1"));
        QVERIFY(generationAfter > generationBefore);
        const auto hits = recoveredService.search(QStringLiteral("蓝色海水沙滩"), 3, &errorMessage);
        QVERIFY2(!hits.isEmpty(), qPrintable(errorMessage));
        QCOMPARE(hits.first().documentKey, QStringLiteral("asset:ocean"));
        manager.closeDatabase();
    }

    void incompatibleContractForcesRebuild()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("contract.usearch"));

        GlobalDatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
        {
            SemanticSearchIndexService service(&manager, indexPath);
            SemanticIndexUpdateResult result;
            QVERIFY2(service.applyChanges(
                         {document(QStringLiteral("folder:archive"),
                                   SearchDocumentType::Folder,
                                   QStringLiteral("folder-archive"),
                                   QStringLiteral("历史纪录片归档目录"),
                                   QStringLiteral("2026-07-14T13:00:00Z"))},
                         {},
                         &result,
                         &errorMessage),
                     qPrintable(errorMessage));
        }
        const auto generationBefore = scalarValue(
            manager.database(),
            QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1"));
        QSqlQuery invalidate(manager.database());
        QVERIFY2(invalidate.exec(QStringLiteral(
                     "UPDATE search_index_state SET schema_version = 999, model_id = 'other-model', "
                     "dimensions = 768, status = 'ready' WHERE singleton = 1")),
                 qPrintable(invalidate.lastError().text()));

        SemanticSearchIndexService recoveredService(&manager, indexPath);
        QVERIFY2(recoveredService.ensureReady(&errorMessage), qPrintable(errorMessage));
        QSqlQuery state(manager.database());
        QVERIFY2(state.exec(QStringLiteral(
                     "SELECT schema_version, model_id, dimensions, generation, status "
                     "FROM search_index_state WHERE singleton = 1")),
                 qPrintable(state.lastError().text()));
        QVERIFY(state.next());
        QCOMPARE(state.value(0).toInt(), cinevault::searchconfig::kSearchIndexSchemaVersion);
        QCOMPARE(state.value(1).toString(), QStringLiteral("BAAI/bge-small-zh-v1.5"));
        QCOMPARE(state.value(2).toInt(), cinevault::searchconfig::kEmbeddingDimensions);
        QVERIFY(state.value(3).toLongLong() > generationBefore);
        QCOMPARE(state.value(4).toString(), QStringLiteral("ready"));
        manager.closeDatabase();
    }
};

QTEST_GUILESS_MAIN(SemanticSearchIndexServiceTest)

#include "SemanticSearchIndexServiceTest.moc"
