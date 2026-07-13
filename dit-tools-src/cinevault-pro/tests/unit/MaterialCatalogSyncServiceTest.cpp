#include "domain/Entities.h"
#include "domain/Enums.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/db/GlobalDatabaseManager.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

bool syncProjectIntoGlobalForTest(QSqlDatabase &globalDb, const Project &project, bool hasFts5, QString *errorMessage);

namespace {
QString globalDatabasePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/material-center.sqlite"));
}

void removeGlobalDatabaseFiles()
{
    const auto path = globalDatabasePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

bool insertProjectRecord(QSqlDatabase db, const Project &project, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO project (id, name, root_path, created_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(project.id);
    query.addBindValue(project.name);
    query.addBindValue(project.rootPath);
    query.addBindValue(project.createdAt);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool insertSourceRoot(QSqlDatabase db, const QString &sourcePath, qint64 *sourceRootId, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, "
        "warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 1, 0, 12, 1, 0, 0, 0, 0, 2, '2026-07-05T10:00:00', '2026-07-05T10:00:00')"));
    query.addBindValue(sourcePath);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (sourceRootId) {
        *sourceRootId = query.lastInsertId().toLongLong();
    }
    return true;
}

bool insertBareVideoAsset(QSqlDatabase db, qint64 sourceRootId, const QString &sourcePath, QString *errorMessage)
{
    const auto filePath = QDir(sourcePath).filePath(QStringLiteral("clip.mov"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建测试文件：%1").arg(filePath);
        }
        return false;
    }
    file.write("video");
    file.close();

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO asset_file "
        "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, size_bytes, modified_at, "
        "is_readable, created_at) "
        "VALUES (?, 'clip.mov', 'mov', ?, 'clip.mov', ?, ?, 12, '2026-07-05T10:00:00', 1, '2026-07-05T10:00:00')"));
    query.addBindValue(sourceRootId);
    query.addBindValue(filePath);
    query.addBindValue(sourcePath);
    query.addBindValue(static_cast<int>(AssetType::Video));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

qint64 firstAssetId(QSqlDatabase db, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM asset_file ORDER BY id LIMIT 1"));
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }
    return query.value(0).toLongLong();
}

bool insertLegacyGlobalAnalysis(QSqlDatabase db,
                                const Project &project,
                                const QString &oldVideoKey,
                                qint64 oldAssetId,
                                const QString &filePath,
                                bool hasFts5,
                                QString *errorMessage)
{
    QSqlQuery registry(db);
    registry.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO project_registry "
        "(project_uuid, project_name, project_database_path, last_synced_at, sync_status, error_message) "
        "VALUES (?, ?, ?, '2026-07-05T09:00:00', 'ok', '')"));
    registry.addBindValue(project.id);
    registry.addBindValue(project.name);
    registry.addBindValue(project.databasePath);
    if (!registry.exec()) {
        if (errorMessage) {
            *errorMessage = registry.lastError().text();
        }
        return false;
    }

    QSqlQuery asset(db);
    asset.prepare(QStringLiteral(
        "INSERT INTO global_video_asset "
        "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
        "asset_id, file_name, extension, absolute_path, relative_path, asset_type, size_bytes, modified_at, duration_ms, "
        "thumbnail_path, thumbnail_status, analysis_status, confirmation_status, technical_summary, source_text, "
        "error_message, last_synced_at, updated_at) "
        "VALUES (?, ?, ?, ?, 1, 'Source', ?, 'clip.mov', 'mov', ?, 'clip.mov', ?, 12, '2026-07-05T10:00:00', 0, "
        "'', 0, ?, 0, '', '', '', '2026-07-05T09:00:00', '2026-07-05T09:00:00')"));
    asset.addBindValue(oldVideoKey);
    asset.addBindValue(project.id);
    asset.addBindValue(project.name);
    asset.addBindValue(project.databasePath);
    asset.addBindValue(oldAssetId);
    asset.addBindValue(filePath);
    asset.addBindValue(static_cast<int>(AssetType::Video));
    asset.addBindValue(static_cast<int>(VideoAnalysisStatus::Ready));
    if (!asset.exec()) {
        if (errorMessage) {
            *errorMessage = asset.lastError().text();
        }
        return false;
    }

    QSqlQuery result(db);
    result.prepare(QStringLiteral(
        "INSERT INTO video_analysis_result "
        "(video_key, summary, keywords_json, scenes_json, search_text, model_name, prompt_version, analyzed_at, confirmed_at) "
        "VALUES (?, '旧解析摘要', '[\"旧关键词\"]', '[\"旧场景\"]', '旧解析摘要 旧关键词', 'test', 'v1', "
        "'2026-07-05T09:01:00', '')"));
    result.addBindValue(oldVideoKey);
    if (!result.exec()) {
        if (errorMessage) {
            *errorMessage = result.lastError().text();
        }
        return false;
    }

    QSqlQuery frame(db);
    frame.prepare(QStringLiteral(
        "INSERT INTO video_frame_analysis "
        "(video_key, frame_number, timestamp_ms, image_path, caption, tags_json, objects_json, actions, setting_text, analysis_state) "
        "VALUES (?, 1, 1000, 'frame.jpg', '旧帧描述', '[\"旧标签\"]', '[\"旧对象\"]', '旧动作', '旧场景', 1)"));
    frame.addBindValue(oldVideoKey);
    if (!frame.exec()) {
        if (errorMessage) {
            *errorMessage = frame.lastError().text();
        }
        return false;
    }

    QSqlQuery task(db);
    task.prepare(QStringLiteral(
        "INSERT INTO video_analysis_task "
        "(video_key, stage, total_frames, completed_frames, successful_frames, skipped_frames, summary_retry_count, "
        "last_error_message, last_updated_at) "
        "VALUES (?, 4, 1, 1, 1, 0, 0, '', '2026-07-05T09:01:00')"));
    task.addBindValue(oldVideoKey);
    if (!task.exec()) {
        if (errorMessage) {
            *errorMessage = task.lastError().text();
        }
        return false;
    }

    QSqlQuery dimension(db);
    dimension.prepare(QStringLiteral(
        "INSERT INTO material_dimension_analysis "
        "(video_key, dimension_key, dimension_name, detail, model_name, prompt_version, analyzed_at) "
        "VALUES (?, 'color-style', '色彩风格', '旧色彩风格补充', 'test', 'v1', '2026-07-05T09:02:00')"));
    dimension.addBindValue(oldVideoKey);
    if (!dimension.exec()) {
        if (errorMessage) {
            *errorMessage = dimension.lastError().text();
        }
        return false;
    }

    QSqlQuery dimensionFrame(db);
    dimensionFrame.prepare(QStringLiteral(
        "INSERT INTO material_dimension_frame_analysis "
        "(video_key, dimension_key, dimension_name, frame_number, timestamp_ms, image_path, detail, error_message, "
        "analysis_state, model_name, prompt_version, analyzed_at) "
        "VALUES (?, 'color-style', '色彩风格', 1, 1000, 'frame.jpg', '旧帧级色彩补充', '', 1, 'test', 'v1-frame', "
        "'2026-07-05T09:02:30')"));
    dimensionFrame.addBindValue(oldVideoKey);
    if (!dimensionFrame.exec()) {
        if (errorMessage) {
            *errorMessage = dimensionFrame.lastError().text();
        }
        return false;
    }

    if (hasFts5) {
        QSqlQuery fts(db);
        fts.prepare(QStringLiteral(
            "INSERT INTO video_search_fts "
            "(video_key, project_name, source_root_name, file_name, relative_path, absolute_path, asset_type_label, "
            "extension, technical_summary, summary, keywords, captions, source_text) "
            "VALUES (?, ?, 'Source', 'clip.mov', 'clip.mov', ?, '视频', 'mov', '', '旧解析摘要', '旧关键词', "
            "'旧帧描述 旧标签', '')"));
        fts.addBindValue(oldVideoKey);
        fts.addBindValue(project.name);
        fts.addBindValue(filePath);
        if (!fts.exec()) {
            if (errorMessage) {
                *errorMessage = fts.lastError().text();
            }
            return false;
        }
    }

    return true;
}
}

class MaterialCatalogSyncServiceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("CineVaultUnitTests"));
        QCoreApplication::setApplicationName(QStringLiteral("MaterialCatalogSyncServiceTest"));
    }

    void cleanup()
    {
        removeGlobalDatabaseFiles();
    }

    void syncCurrentProject_writesEmptyStringsForMissingAnalysisFields()
    {
        removeGlobalDatabaseFiles();

        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-sync");
        project.name = QStringLiteral("SyncProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-05T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(sourcePath));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertBareVideoAsset(databaseManager.database(), sourceRootId, sourcePath, &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));

        QSqlQuery query(globalDb);
        query.prepare(QStringLiteral(
            "SELECT technical_summary, technical_summary IS NULL, source_text, source_text IS NULL, error_message "
            "FROM global_video_asset WHERE project_uuid = ?"));
        query.addBindValue(project.id);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString());
        QCOMPARE(query.value(1).toInt(), 0);
        QCOMPARE(query.value(2).toString(), QString());
        QCOMPARE(query.value(3).toInt(), 0);
        QCOMPARE(query.value(4).toString(), QString());
        QVERIFY(!query.next());

        if (globalDatabaseManager.hasFts5()) {
            QSqlQuery ftsQuery(globalDb);
            ftsQuery.prepare(QStringLiteral("SELECT source_text FROM video_search_fts WHERE project_name = 'SyncProject'"));
            QVERIFY2(ftsQuery.exec(), qPrintable(ftsQuery.lastError().text()));
            QVERIFY(ftsQuery.next());
            QCOMPARE(ftsQuery.value(0).toString(), QString());
        }

        globalDatabaseManager.closeDatabase();
    }

    void syncCurrentProject_migratesAnalysisWhenAssetIdChangesForSamePath()
    {
        removeGlobalDatabaseFiles();

        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-rekey");
        project.name = QStringLiteral("RekeyProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-05T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(sourcePath));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertBareVideoAsset(databaseManager.database(), sourceRootId, sourcePath, &errorMessage),
                 qPrintable(errorMessage));
        const auto newAssetId = firstAssetId(databaseManager.database(), &errorMessage);
        QVERIFY2(newAssetId > 0, qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        const auto oldVideoKey = QStringLiteral("%1:%2").arg(project.id).arg(999);
        const auto newVideoKey = QStringLiteral("%1:%2").arg(project.id).arg(newAssetId);
        const auto filePath = QDir(sourcePath).filePath(QStringLiteral("clip.mov"));
        QVERIFY2(insertLegacyGlobalAnalysis(globalDb,
                                            project,
                                            oldVideoKey,
                                            999,
                                            filePath,
                                            globalDatabaseManager.hasFts5(),
                                            &errorMessage),
                 qPrintable(errorMessage));

        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));

        QSqlQuery oldAsset(globalDb);
        oldAsset.prepare(QStringLiteral("SELECT COUNT(*) FROM global_video_asset WHERE video_key = ?"));
        oldAsset.addBindValue(oldVideoKey);
        QVERIFY2(oldAsset.exec(), qPrintable(oldAsset.lastError().text()));
        QVERIFY(oldAsset.next());
        QCOMPARE(oldAsset.value(0).toInt(), 0);

        QSqlQuery result(globalDb);
        result.prepare(QStringLiteral("SELECT summary FROM video_analysis_result WHERE video_key = ?"));
        result.addBindValue(newVideoKey);
        QVERIFY2(result.exec(), qPrintable(result.lastError().text()));
        QVERIFY(result.next());
        QCOMPARE(result.value(0).toString(), QStringLiteral("旧解析摘要"));

        QSqlQuery frame(globalDb);
        frame.prepare(QStringLiteral("SELECT caption FROM video_frame_analysis WHERE video_key = ?"));
        frame.addBindValue(newVideoKey);
        QVERIFY2(frame.exec(), qPrintable(frame.lastError().text()));
        QVERIFY(frame.next());
        QCOMPARE(frame.value(0).toString(), QStringLiteral("旧帧描述"));

        QSqlQuery dimension(globalDb);
        dimension.prepare(QStringLiteral("SELECT detail FROM material_dimension_analysis WHERE video_key = ?"));
        dimension.addBindValue(newVideoKey);
        QVERIFY2(dimension.exec(), qPrintable(dimension.lastError().text()));
        QVERIFY(dimension.next());
        QCOMPARE(dimension.value(0).toString(), QStringLiteral("旧色彩风格补充"));

        QSqlQuery dimensionFrame(globalDb);
        dimensionFrame.prepare(QStringLiteral("SELECT detail FROM material_dimension_frame_analysis WHERE video_key = ?"));
        dimensionFrame.addBindValue(newVideoKey);
        QVERIFY2(dimensionFrame.exec(), qPrintable(dimensionFrame.lastError().text()));
        QVERIFY(dimensionFrame.next());
        QCOMPARE(dimensionFrame.value(0).toString(), QStringLiteral("旧帧级色彩补充"));

        QSqlQuery asset(globalDb);
        asset.prepare(QStringLiteral("SELECT analysis_status FROM global_video_asset WHERE video_key = ?"));
        asset.addBindValue(newVideoKey);
        QVERIFY2(asset.exec(), qPrintable(asset.lastError().text()));
        QVERIFY(asset.next());
        QCOMPARE(asset.value(0).toInt(), static_cast<int>(VideoAnalysisStatus::Ready));

        if (globalDatabaseManager.hasFts5()) {
            QSqlQuery fts(globalDb);
            fts.prepare(QStringLiteral("SELECT summary, captions FROM video_search_fts WHERE video_key = ?"));
            fts.addBindValue(newVideoKey);
            QVERIFY2(fts.exec(), qPrintable(fts.lastError().text()));
            QVERIFY(fts.next());
            QCOMPARE(fts.value(0).toString(), QStringLiteral("旧解析摘要"));
            QVERIFY(fts.value(1).toString().contains(QStringLiteral("旧帧描述")));
        }

        globalDatabaseManager.closeDatabase();
    }

    void globalDatabaseManager_backfillsLegacyFrameAnalysisState()
    {
        removeGlobalDatabaseFiles();

        QString errorMessage;
        {
            GlobalDatabaseManager globalDatabaseManager;
            QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = globalDatabaseManager.database();

            QSqlQuery project(db);
            project.prepare(QStringLiteral(
                "INSERT INTO project_registry "
                "(project_uuid, project_name, project_database_path, last_synced_at, sync_status, error_message) "
                "VALUES ('project-legacy', 'LegacyProject', 'legacy.cvdb', '2026-07-05T10:00:00', 'ok', '')"));
            QVERIFY2(project.exec(), qPrintable(project.lastError().text()));

            QSqlQuery asset(db);
            asset.prepare(QStringLiteral(
                "INSERT INTO global_video_asset "
                "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                "asset_id, file_name, absolute_path, relative_path, size_bytes, modified_at, duration_ms, "
                "analysis_status, confirmation_status, last_synced_at, updated_at) "
                "VALUES ('legacy-video', 'project-legacy', 'LegacyProject', 'legacy.cvdb', 1, 'Source', "
                "1, 'clip.mov', 'C:/media/clip.mov', 'clip.mov', 12, '2026-07-05T10:00:00', 1000, "
                "2, 0, '2026-07-05T10:00:00', '2026-07-05T10:00:00')"));
            QVERIFY2(asset.exec(), qPrintable(asset.lastError().text()));

            QSqlQuery frame(db);
            frame.prepare(QStringLiteral(
                "INSERT INTO video_frame_analysis "
                "(video_key, frame_number, timestamp_ms, image_path, caption, tags_json, objects_json, "
                "actions, setting_text, error_message, analysis_state) "
                "VALUES "
                "('legacy-video', 1, 100, 'frame1.jpg', '旧帧描述', '[]', '[]', '', '', '', 0), "
                "('legacy-video', 2, 200, 'frame2.jpg', '', '[\"服装\"]', '[]', '', '', '', 0), "
                "('legacy-video', 3, 300, 'frame3.jpg', '', '[]', '[]', '', '', '视觉接口返回内容不是有效 JSON', 0), "
                "('legacy-video', 4, 400, 'frame4.jpg', '', '[]', '[]', '', '', '', 0)"));
            QVERIFY2(frame.exec(), qPrintable(frame.lastError().text()));

            QSqlQuery version(db);
            QVERIFY2(version.exec(QStringLiteral("UPDATE schema_version SET version = 6")),
                     qPrintable(version.lastError().text()));
            globalDatabaseManager.closeDatabase();
        }

        {
            GlobalDatabaseManager globalDatabaseManager;
            QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = globalDatabaseManager.database();

            QSqlQuery version(db);
            QVERIFY2(version.exec(QStringLiteral("SELECT version FROM schema_version")),
                     qPrintable(version.lastError().text()));
            QVERIFY(version.next());
            QCOMPARE(version.value(0).toInt(), 7);

            QSqlQuery states(db);
            QVERIFY2(states.exec(QStringLiteral(
                         "SELECT frame_number, analysis_state FROM video_frame_analysis "
                         "WHERE video_key = 'legacy-video' ORDER BY frame_number")),
                     qPrintable(states.lastError().text()));

            QVERIFY(states.next());
            QCOMPARE(states.value(0).toInt(), 1);
            QCOMPARE(states.value(1).toInt(), static_cast<int>(FrameAnalysisState::Success));
            QVERIFY(states.next());
            QCOMPARE(states.value(0).toInt(), 2);
            QCOMPARE(states.value(1).toInt(), static_cast<int>(FrameAnalysisState::Success));
            QVERIFY(states.next());
            QCOMPARE(states.value(0).toInt(), 3);
            QCOMPARE(states.value(1).toInt(), static_cast<int>(FrameAnalysisState::Failed));
            QVERIFY(states.next());
            QCOMPARE(states.value(0).toInt(), 4);
            QCOMPARE(states.value(1).toInt(), static_cast<int>(FrameAnalysisState::Pending));
            QVERIFY(!states.next());

            globalDatabaseManager.closeDatabase();
        }
    }
};

QTEST_GUILESS_MAIN(MaterialCatalogSyncServiceTest)

#include "MaterialCatalogSyncServiceTest.moc"
