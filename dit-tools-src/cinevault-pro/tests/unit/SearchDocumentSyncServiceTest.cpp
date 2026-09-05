#include "application/IndexingWorkCoordinator.h"
#include "application/SearchDocumentSyncService.h"
#include "core/search/SemanticSearchIndexService.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {
QString globalDatabasePath()
{
    return QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("material-center.sqlite"));
}

bool execute(QSqlDatabase db, const QString &statement, QString *errorMessage)
{
    QSqlQuery query(db);
    if (query.exec(statement)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

class Fixture {
public:
    Fixture()
    {
        QFile::remove(globalDatabasePath());
        if (!manager.openDatabase(&errorMessage)) {
            return;
        }
        valid = seed();
    }

    ~Fixture()
    {
        manager.closeDatabase();
        QFile::remove(globalDatabasePath());
    }

    GlobalDatabaseManager manager;
    bool valid = false;
    QString errorMessage;

private:
    bool seed()
    {
        const QStringList statements{
            QStringLiteral(
                "INSERT INTO project_registry(project_uuid, project_name, project_database_path, sync_status) "
                "VALUES ('project-search', '雪山广告项目', 'G:/projects/search/project.sqlite', 'success')"),
            QStringLiteral(
                "INSERT INTO global_folder_node("
                "folder_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                "name, absolute_path, path_key, relative_path, normalized_date, updated_at) VALUES "
                "('project-search|1|2026-07-14/山顶日出', 'project-search', '雪山广告项目', 'G:/projects/search/project.sqlite', "
                "1, '主摄影机', '山顶日出', 'G:/projects/search/2026-07-14/山顶日出', "
                "'g:/projects/search/2026-07-14/山顶日出', '2026-07-14/山顶日出', "
                "'2026-07-14', '2026-07-14T08:00:00')"),
            QStringLiteral(
                "INSERT INTO global_video_asset("
                "video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                "asset_id, file_name, extension, absolute_path, relative_path, asset_type, modified_at, "
                "technical_summary, embedded_metadata_text, source_text, updated_at) VALUES "
                "('project-search:1', 'project-search', '雪山广告项目', 'G:/projects/search/project.sqlite', "
                "1, '主摄影机', 1, 'clip001.mov', 'mov', 'G:/projects/search/clip001.mov', "
                "'山顶日出/clip001.mov', 1, '2026-07-14T07:00:00', 'ProRes 4K', "
                "'EXIF:Make Sony EXIF:Model AlphaA7M4', '', "
                "'2026-07-14T08:00:00'), "
                "('project-search:2', 'project-search', '雪山广告项目', 'G:/projects/search/project.sqlite', "
                "1, '制作文档', 2, 'contract.md', 'md', 'G:/projects/search/contract.md', "
                "'docs/contract.md', 6, '2026-07-14T07:00:00', 'Markdown 文档', '', "
                "'品牌独家授权合同', '2026-07-14T08:00:00')"),
            QStringLiteral(
                "INSERT INTO video_analysis_result("
                "video_key, summary, keywords_json, scenes_json, search_text, analyzed_at) VALUES "
                "('project-search:1', '模特在雪山山顶观看日出', '[\"雪山\",\"日出\"]', "
                "'[\"山顶\"]', '雪山山顶日出 户外广告', '2026-07-14T09:00:00')"),
            QStringLiteral(
                "INSERT INTO video_frame_analysis("
                "video_key, frame_number, caption, tags_json, objects_json, actions, setting_text, "
                "entities_json, ocr_text, structured_profile_version, facts_complete, analysis_state, analyzed_at) VALUES "
                "('project-search:1', 1, '红色牛仔短裤模特站在雪山山顶', '[\"日出\"]', '[\"人物\"]', "
                "'观看日出', '雪山户外', "
                "'[{\"label\":\"短裤\",\"colors\":[\"红色\"],\"materials\":[\"牛仔\"],\"attributes\":[]}]', "
                "'SUMMIT', 2, 1, 1, '2026-07-14T09:05:00')"),
            QStringLiteral(
                "INSERT INTO material_dimension_analysis("
                "video_key, dimension_key, dimension_name, detail, analyzed_at) VALUES "
                "('project-search:1', 'brand', '品牌适配', '适合户外运动广告', '2026-07-14T09:10:00')")
        };
        for (const auto &statement : statements) {
            if (!execute(manager.database(), statement, &errorMessage)) {
                return false;
            }
        }
        return true;
    }
};

qint64 scalarCount(QSqlDatabase db, const QString &statement)
{
    QSqlQuery query(db);
    return query.exec(statement) && query.next() ? query.value(0).toLongLong() : -1;
}
}

class SearchDocumentSyncServiceTest : public QObject {
    Q_OBJECT

private slots:
    void emptyCatalogBuildsReadyBoundedIndex()
    {
        QFile::remove(globalDatabasePath());
        GlobalDatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("empty.usearch"));
        SemanticSearchIndexService semanticIndex(&manager, indexPath);
        SearchDocumentSyncService syncService(&manager, &semanticIndex);

        SemanticIndexUpdateResult result;
        QVERIFY2(syncService.synchronizeNow(&result, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(result.inserted, 0);
        QCOMPARE(result.updated, 0);
        QCOMPARE(result.removed, 0);
        QCOMPARE(scalarCount(manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{0});
        QSqlQuery state(manager.database());
        QVERIFY2(state.exec(QStringLiteral(
                     "SELECT status, document_count FROM search_index_state WHERE singleton = 1")),
                 qPrintable(state.lastError().text()));
        QVERIFY(state.next());
        QCOMPARE(state.value(0).toString(), QStringLiteral("ready"));
        QCOMPARE(state.value(1).toInt(), 0);
        QVERIFY(QFile::exists(indexPath));
        manager.closeDatabase();
        QFile::remove(globalDatabasePath());
    }

    void aggregatesContentAndMaintainsPersistentIndexIncrementally()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("documents.usearch"));
        SemanticSearchIndexService semanticIndex(&fixture.manager, indexPath);
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);

        QString errorMessage;
        SemanticIndexUpdateResult first;
        QVERIFY2(syncService.synchronizeNow(&first, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(first.inserted, 4);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{4});

        QSqlQuery contentQuery(fixture.manager.database());
        contentQuery.prepare(QStringLiteral(
            "SELECT content_text, source_updated_at FROM search_document WHERE document_key = ?"));
        contentQuery.addBindValue(QStringLiteral("asset:project-search:1"));
        QVERIFY(contentQuery.exec());
        QVERIFY(contentQuery.next());
        const auto content = contentQuery.value(0).toString();
        QVERIFY(content.contains(QStringLiteral("红色牛仔短裤")));
        QVERIFY(content.contains(QStringLiteral("适合户外运动广告")));
        QVERIFY(content.contains(QStringLiteral("SUMMIT")));
        QVERIFY(content.contains(QStringLiteral("AlphaA7M4")));
        QCOMPARE(contentQuery.value(1).toString(), QStringLiteral("2026-07-14T09:10:00"));

        QSqlQuery frameDocumentQuery(fixture.manager.database());
        frameDocumentQuery.prepare(QStringLiteral(
            "SELECT document_type, entity_key, content_text FROM search_document "
            "WHERE document_key = 'frame:project-search:1:1'"));
        QVERIFY(frameDocumentQuery.exec());
        QVERIFY(frameDocumentQuery.next());
        QCOMPARE(frameDocumentQuery.value(0).toInt(),
                 static_cast<int>(SearchDocumentType::VisualEntity));
        QCOMPARE(frameDocumentQuery.value(1).toString(), QStringLiteral("project-search:1"));
        QVERIFY(frameDocumentQuery.value(2).toString().contains(QStringLiteral("红色牛仔短裤")));

        const auto semanticHits = semanticIndex.search(QStringLiteral("雪山山顶日出户外广告"),
                                                       3,
                                                       &errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
        QVERIFY(std::any_of(semanticHits.cbegin(), semanticHits.cend(), [](const auto &hit) {
            return hit.documentKey == QStringLiteral("asset:project-search:1");
        }));

        SemanticIndexUpdateResult unchanged;
        QVERIFY2(syncService.synchronizeNow(&unchanged, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(unchanged.unchanged, 4);
        QCOMPARE(unchanged.inserted, 0);
        QCOMPARE(unchanged.updated, 0);

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral(
                            "UPDATE video_analysis_result SET summary = '模特在金色晨光中的雪山山顶观看日出', "
                            "analyzed_at = '2026-07-14T10:00:00' WHERE video_key = 'project-search:1'"),
                        &errorMessage));
        SemanticIndexUpdateResult updated;
        QVERIFY2(syncService.synchronizeNow(&updated, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(updated.updated, 1);
        QCOMPARE(updated.unchanged, 3);

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral("DELETE FROM global_video_asset WHERE video_key = 'project-search:2'"),
                        &errorMessage));
        SemanticIndexUpdateResult removed;
        QVERIFY2(syncService.synchronizeNow(&removed, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(removed.removed, 1);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{3});
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document "
                                             "WHERE document_key = 'asset:project-search:2'")),
                 qint64{0});
        QVERIFY(QFile::exists(indexPath));
    }

    void scheduledSyncUsesBackgroundConnectionAndPublishesCompletion()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        SemanticSearchIndexService semanticIndex(
            &fixture.manager,
            QDir(temp.path()).filePath(QStringLiteral("background.usearch")));
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);
        QSignalSpy completion(&syncService,
                              &SearchDocumentSyncService::synchronizationFinished);
        QSignalSpy progress(&syncService,
                            &SearchDocumentSyncService::synchronizationProgress);

        QElapsedTimer debounceElapsed;
        debounceElapsed.start();
        syncService.scheduleFullSync();

        QVERIFY2(completion.wait(10000), "后台搜索文档同步未在超时前完成");
        QVERIFY(debounceElapsed.elapsed() >= 2000);
        QVERIFY(debounceElapsed.elapsed() < 10000);
        const auto arguments = completion.takeFirst();
        QVERIFY(arguments.at(0).toBool());
        QCOMPARE(arguments.at(1).toInt(), 4);
        QVERIFY(!progress.isEmpty());
        bool reportedDocumentTotal = false;
        for (const auto &update : progress) {
            if (update.at(1).toInt() == 4) {
                reportedDocumentTotal = true;
                break;
            }
        }
        QVERIFY(reportedDocumentTotal);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{4});
    }

    void backgroundSyncWaitsSilentlyUntilIdle()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        SemanticSearchIndexService semanticIndex(
            &fixture.manager,
            QDir(temp.path()).filePath(QStringLiteral("background-idle.usearch")));
        IndexingWorkCoordinator coordinator;
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);
        syncService.setWorkCoordinator(&coordinator);
        QSignalSpy completion(&syncService,
                              &SearchDocumentSyncService::synchronizationFinished);
        QSignalSpy progress(&syncService,
                            &SearchDocumentSyncService::synchronizationProgress);

        syncService.scheduleFullSync();
        QTest::qWait(3000);
        QCOMPARE(progress.size(), 0);
        QCOMPARE(completion.size(), 0);

        coordinator.setSystemIdle(true);
        syncService.resumePendingWork();
        QVERIFY2(completion.wait(10000), "系统空闲后未继续执行静默语义索引");
        QVERIFY(!progress.isEmpty());
        const auto arguments = completion.takeFirst();
        QVERIFY(arguments.at(0).toBool());
        QCOMPARE(arguments.at(1).toInt(), 4);
    }

    void immediateSyncBypassesIdleGate()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        SemanticSearchIndexService semanticIndex(
            &fixture.manager,
            QDir(temp.path()).filePath(QStringLiteral("manual-immediate.usearch")));
        IndexingWorkCoordinator coordinator;
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);
        syncService.setWorkCoordinator(&coordinator);
        QSignalSpy completion(&syncService,
                              &SearchDocumentSyncService::synchronizationFinished);
        QSignalSpy progress(&syncService,
                            &SearchDocumentSyncService::synchronizationProgress);

        syncService.scheduleFullSync();
        QElapsedTimer immediateElapsed;
        immediateElapsed.start();
        syncService.scheduleImmediateFullSync();
        QVERIFY2(completion.wait(10000), "手动语义索引未绕过空闲门槛");
        QVERIFY(immediateElapsed.elapsed() < 10000);
        QVERIFY(!progress.isEmpty());
        const auto arguments = completion.takeFirst();
        QVERIFY(arguments.at(0).toBool());
        QCOMPARE(arguments.at(1).toInt(), 4);
    }

    void materialDelta_skipsThumbnailVectorsAndUpdatesOnlyChangedDocuments()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        SemanticSearchIndexService semanticIndex(
            &fixture.manager,
            QDir(temp.path()).filePath(QStringLiteral("delta.usearch")));
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);

        QString errorMessage;
        SemanticIndexUpdateResult initial;
        QVERIFY2(syncService.synchronizeNow(&initial, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(initial.inserted, 4);

        QSqlQuery before(fixture.manager.database());
        QVERIFY2(before.exec(QStringLiteral(
                     "SELECT content_hash, indexed_at FROM search_document "
                     "WHERE document_key = 'asset:project-search:1'")),
                 qPrintable(before.lastError().text()));
        QVERIFY(before.next());
        const auto originalHash = before.value(0).toString();
        const auto originalIndexedAt = before.value(1).toString();
        const auto originalGeneration = scalarCount(
            fixture.manager.database(),
            QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1"));

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral(
                            "UPDATE global_video_asset SET thumbnail_path = 'new-thumb.jpg', "
                            "thumbnail_status = 1, updated_at = '2026-07-14T11:00:00' "
                            "WHERE video_key = 'project-search:1'"),
                        &errorMessage));
        CatalogChangeSet thumbnailChangeSet;
        thumbnailChangeSet.projectUuid = QStringLiteral("project-search");
        CatalogChange thumbnailChange;
        thumbnailChange.entity = CatalogChangeEntity::Asset;
        thumbnailChange.operation = CatalogChangeOperation::Updated;
        thumbnailChange.entityId = 1;
        thumbnailChange.changeMask = CatalogChangeMask::Thumbnail;
        thumbnailChangeSet.changes.append(thumbnailChange);

        SemanticIndexUpdateResult thumbnailResult;
        QVERIFY2(syncService.synchronizeChangesNow(
                     thumbnailChangeSet, &thumbnailResult, &errorMessage),
                 qPrintable(errorMessage));
        QCOMPARE(thumbnailResult.inserted, 0);
        QCOMPARE(thumbnailResult.updated, 0);
        QCOMPARE(thumbnailResult.removed, 0);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1")),
                 originalGeneration);

        QSqlQuery unchanged(fixture.manager.database());
        QVERIFY2(unchanged.exec(QStringLiteral(
                     "SELECT content_hash, indexed_at FROM search_document "
                     "WHERE document_key = 'asset:project-search:1'")),
                 qPrintable(unchanged.lastError().text()));
        QVERIFY(unchanged.next());
        QCOMPARE(unchanged.value(0).toString(), originalHash);
        QCOMPARE(unchanged.value(1).toString(), originalIndexedAt);

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral(
                            "UPDATE global_video_asset SET embedded_metadata_text = "
                            "'EXIF:Make Sony EXIF:Model AlphaA7M4 新增镜头信息', "
                            "updated_at = '2026-07-14T11:01:00' "
                            "WHERE video_key = 'project-search:1'"),
                        &errorMessage));
        auto metadataChangeSet = thumbnailChangeSet;
        metadataChangeSet.changes[0].changeMask = CatalogChangeMask::AssetMetadata;
        SemanticIndexUpdateResult metadataResult;
        QVERIFY2(syncService.synchronizeChangesNow(
                     metadataChangeSet, &metadataResult, &errorMessage),
                 qPrintable(errorMessage));
        QCOMPARE(metadataResult.updated, 1);
        QCOMPARE(metadataResult.unchanged, 1);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT generation FROM search_index_state WHERE singleton = 1")),
                 originalGeneration + 1);

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral(
                            "DELETE FROM global_video_asset WHERE video_key = 'project-search:2'"),
                        &errorMessage));
        CatalogChangeSet removalChangeSet;
        removalChangeSet.projectUuid = QStringLiteral("project-search");
        CatalogChange removalChange;
        removalChange.entity = CatalogChangeEntity::Asset;
        removalChange.operation = CatalogChangeOperation::Removed;
        removalChange.entityId = 2;
        removalChange.changeMask = CatalogChangeMask::AssetCore;
        removalChangeSet.changes.append(removalChange);
        SemanticIndexUpdateResult removalResult;
        QVERIFY2(syncService.synchronizeChangesNow(
                     removalChangeSet, &removalResult, &errorMessage),
                 qPrintable(errorMessage));
        QCOMPARE(removalResult.removed, 1);
        QCOMPARE(removalResult.updated, 0);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{3});
    }

    void scheduledMetadataDeltasCoalesceForTwoToFiveSeconds()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        SemanticSearchIndexService semanticIndex(
            &fixture.manager,
            QDir(temp.path()).filePath(QStringLiteral("delta-debounce.usearch")));
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);
        QString errorMessage;
        SemanticIndexUpdateResult initial;
        QVERIFY2(syncService.synchronizeNow(&initial, &errorMessage), qPrintable(errorMessage));

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral(
                            "UPDATE global_video_asset SET embedded_metadata_text = 'coalesced metadata', "
                            "updated_at = '2026-07-14T12:00:00' "
                            "WHERE video_key = 'project-search:1'"),
                        &errorMessage));
        CatalogChangeSet changeSet;
        changeSet.projectUuid = QStringLiteral("project-search");
        CatalogChange change;
        change.entity = CatalogChangeEntity::Asset;
        change.operation = CatalogChangeOperation::Updated;
        change.entityId = 1;
        change.changeMask = CatalogChangeMask::AssetMetadata;
        changeSet.changes.append(change);

        QSignalSpy completion(&syncService,
                              &SearchDocumentSyncService::synchronizationFinished);
        QElapsedTimer elapsed;
        elapsed.start();
        syncService.scheduleCatalogChanges(changeSet);
        changeSet.changes[0].changeMask = CatalogChangeMask::AssetCore;
        syncService.scheduleCatalogChanges(changeSet);
        QVERIFY2(completion.wait(10000), "合并后的 metadata delta 未在超时前完成");
        QVERIFY(elapsed.elapsed() >= 2000);
        QVERIFY(elapsed.elapsed() < 10000);
        QCOMPARE(completion.size(), 1);
        const auto arguments = completion.takeFirst();
        QVERIFY(arguments.at(0).toBool());
        QCOMPARE(arguments.at(1).toInt(), 0);
        QCOMPARE(arguments.at(2).toInt(), 1);
        QCOMPARE(arguments.at(3).toInt(), 1);
    }

    void folderDeltaReplacesOnlyRenamedFolderDocument()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        SemanticSearchIndexService semanticIndex(
            &fixture.manager,
            QDir(temp.path()).filePath(QStringLiteral("folder-delta.usearch")));
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);
        QString errorMessage;
        SemanticIndexUpdateResult initial;
        QVERIFY2(syncService.synchronizeNow(&initial, &errorMessage), qPrintable(errorMessage));

        QVERIFY(execute(fixture.manager.database(),
                        QStringLiteral(
                            "UPDATE global_folder_node SET "
                            "folder_key = 'project-search|1|2026-07-14/山谷', "
                            "name = '山谷', relative_path = '2026-07-14/山谷', "
                            "absolute_path = 'G:/projects/search/2026-07-14/山谷', "
                            "updated_at = '2026-07-14T12:30:00' "
                            "WHERE folder_key = 'project-search|1|2026-07-14/山顶日出'"),
                        &errorMessage));

        CatalogChangeSet changeSet;
        changeSet.projectUuid = QStringLiteral("project-search");
        CatalogChange change;
        change.entity = CatalogChangeEntity::Folder;
        change.operation = CatalogChangeOperation::Updated;
        change.entityId = 1;
        change.entityKey = QStringLiteral("2026-07-14/山谷");
        change.previousEntityKey = QStringLiteral("2026-07-14/山顶日出");
        change.sourceRootId = 1;
        change.previousSourceRootId = 1;
        change.changeMask = CatalogChangeMask::Folder;
        changeSet.changes.append(change);

        SemanticIndexUpdateResult delta;
        QVERIFY2(syncService.synchronizeChangesNow(changeSet, &delta, &errorMessage),
                 qPrintable(errorMessage));
        QCOMPARE(delta.inserted, 1);
        QCOMPARE(delta.removed, 1);
        QCOMPARE(delta.updated, 0);
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{4});
        QCOMPARE(scalarCount(
                     fixture.manager.database(),
                     QStringLiteral(
                         "SELECT COUNT(*) FROM search_document WHERE document_key = "
                         "'folder:project-search|1|2026-07-14/山顶日出'")),
                 qint64{0});
        QCOMPARE(scalarCount(
                     fixture.manager.database(),
                     QStringLiteral(
                         "SELECT COUNT(*) FROM search_document WHERE document_key = "
                         "'folder:project-search|1|2026-07-14/山谷'")),
                 qint64{1});
    }

    void failedScheduledSyncRestoresWorkAndRetriesWithBackoff()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto indexPath = QDir(temp.path()).filePath(QStringLiteral("retry.usearch"));
        QVERIFY(QDir().mkpath(indexPath));
        SemanticSearchIndexService semanticIndex(&fixture.manager, indexPath);
        SearchDocumentSyncService syncService(&fixture.manager, &semanticIndex);
        QSignalSpy completion(&syncService,
                              &SearchDocumentSyncService::synchronizationFinished);

        syncService.scheduleImmediateFullSync();
        QVERIFY2(completion.wait(10000), "故障同步未返回失败结果");
        QVERIFY(!completion.constFirst().at(0).toBool());

        QVERIFY(QDir(indexPath).removeRecursively());
        QVERIFY2(completion.wait(10000), "修复输出路径后，失败工作未自动重放");
        QCOMPARE(completion.size(), 2);
        QVERIFY(completion.at(1).at(0).toBool());
        QCOMPARE(scalarCount(fixture.manager.database(),
                             QStringLiteral("SELECT COUNT(*) FROM search_document")),
                 qint64{4});
        QVERIFY(QFileInfo(indexPath).isFile());
    }
};

QTEST_GUILESS_MAIN(SearchDocumentSyncServiceTest)

#include "SearchDocumentSyncServiceTest.moc"
