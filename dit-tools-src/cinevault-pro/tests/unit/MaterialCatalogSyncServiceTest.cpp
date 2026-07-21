#include "application/MaterialCatalogSyncService.h"
#include "domain/Entities.h"
#include "domain/Enums.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/FolderPathMetadata.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

bool syncProjectIntoGlobalForTest(QSqlDatabase &globalDb, const Project &project, bool hasFts5, QString *errorMessage);
bool syncProjectIntoGlobalWithDeltasForTest(QSqlDatabase &globalDb,
                                            const Project &project,
                                            bool hasFts5,
                                            QVector<CatalogChangeSet> *changeSets,
                                            QString *errorMessage);
bool rebuildProjectIntoGlobalForTest(QSqlDatabase &globalDb,
                                     const Project &project,
                                     bool hasFts5,
                                     QString *errorMessage);

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
    QFile::remove(path + QStringLiteral(".pre-v8.bak"));
    QFile::remove(path + QStringLiteral(".pre-v9.bak"));
    QFile::remove(path + QStringLiteral(".pre-v11.bak"));
    QFile::remove(path + QStringLiteral(".pre-v13.bak"));
    QFile::remove(path + QStringLiteral(".pre-v14.bak"));
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

bool insertFolderNode(QSqlDatabase db,
                      qint64 sourceRootId,
                      const QString &sourcePath,
                      const QString &relativePath,
                      qint64 directFileCount,
                      qint64 recursiveFileCount,
                      QString *errorMessage)
{
    const auto normalizedRelativePath = FolderPathMetadata::normalizeRelativePath(relativePath);
    const auto absolutePath = normalizedRelativePath.isEmpty()
        ? sourcePath
        : QDir(sourcePath).filePath(normalizedRelativePath);
    const auto date = FolderPathMetadata::inferDate(QStringLiteral("Source"), normalizedRelativePath);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO folder_node "
        "(source_root_id, name, absolute_path, path_key, relative_path, parent_relative_path, depth, file_count, "
        "direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '2026-07-05T10:00:00', '2026-07-05T10:00:00')"));
    query.addBindValue(sourceRootId);
    query.addBindValue(FolderPathMetadata::folderName(absolutePath, QStringLiteral("Source")));
    query.addBindValue(absolutePath);
    query.addBindValue(FolderPathMetadata::normalizedPathKey(absolutePath));
    query.addBindValue(normalizedRelativePath);
    query.addBindValue(FolderPathMetadata::parentRelativePath(normalizedRelativePath));
    query.addBindValue(FolderPathMetadata::depth(normalizedRelativePath));
    query.addBindValue(directFileCount);
    query.addBindValue(directFileCount);
    query.addBindValue(recursiveFileCount);
    query.addBindValue(date.normalizedDate);
    query.addBindValue(date.anchorRelativePath);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool insertVideoAssetAt(QSqlDatabase db,
                        qint64 sourceRootId,
                        const QString &sourcePath,
                        const QString &relativePath,
                        QString *errorMessage)
{
    const auto normalizedRelativePath = FolderPathMetadata::normalizeRelativePath(relativePath);
    const auto filePath = QDir(sourcePath).filePath(normalizedRelativePath);
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建测试目录：%1").arg(QFileInfo(filePath).absolutePath());
        }
        return false;
    }
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
        "is_readable, created_at) VALUES (?, ?, 'mov', ?, ?, ?, ?, 12, '2026-07-05T10:00:00', 1, '2026-07-05T10:00:00')"));
    query.addBindValue(sourceRootId);
    query.addBindValue(QFileInfo(filePath).fileName());
    query.addBindValue(filePath);
    query.addBindValue(normalizedRelativePath);
    query.addBindValue(QFileInfo(filePath).absolutePath());
    query.addBindValue(static_cast<int>(AssetType::Video));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
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

bool insertSyntheticAssets(QSqlDatabase db,
                           qint64 sourceRootId,
                           const QString &sourcePath,
                           int count,
                           QString *errorMessage)
{
    if (!db.transaction()) {
        if (errorMessage) *errorMessage = db.lastError().text();
        return false;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO asset_file "
        "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, "
        "size_bytes, modified_at, is_readable, created_at) "
        "VALUES (?, ?, 'mov', ?, ?, ?, ?, ?, ?, 1, '2026-07-05T10:00:00')"));
    for (int index = 0; index < count; ++index) {
        const auto name = QStringLiteral("clip-%1.mov").arg(index, 6, 10, QLatin1Char('0'));
        const auto path = QDir(sourcePath).filePath(name);
        query.addBindValue(sourceRootId);
        query.addBindValue(name);
        query.addBindValue(path);
        query.addBindValue(name);
        query.addBindValue(sourcePath);
        query.addBindValue(static_cast<int>(AssetType::Video));
        query.addBindValue(100 + index);
        query.addBindValue(QStringLiteral("2026-07-05T10:%1:00").arg(index % 60, 2, 10, QLatin1Char('0')));
        if (!query.exec()) {
            if (errorMessage) *errorMessage = query.lastError().text();
            db.rollback();
            return false;
        }
        query.finish();
    }
    if (!db.commit()) {
        if (errorMessage) *errorMessage = db.lastError().text();
        db.rollback();
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

    QSqlQuery plan(db);
    plan.prepare(QStringLiteral(
        "INSERT INTO video_analysis_plan "
        "(video_key, sampling_policy, frame_interval, structured_profile_version, source_frame_count, planned_frame_count, "
        "asset_size_bytes, asset_modified_at, created_at, updated_at) "
        "VALUES (?, 'fixed_interval', 10, 2, 25, 3, 12, '2026-07-05T10:00:00', "
        "'2026-07-05T09:00:00', '2026-07-05T09:00:00')"));
    plan.addBindValue(oldVideoKey);
    if (!plan.exec()) {
        if (errorMessage) {
            *errorMessage = plan.lastError().text();
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
        QSqlQuery makePdf(databaseManager.database());
        makePdf.prepare(QStringLiteral("UPDATE asset_file SET extension = 'pdf', asset_type = ?"));
        makePdf.addBindValue(static_cast<int>(AssetType::Document));
        QVERIFY2(makePdf.exec(), qPrintable(makePdf.lastError().text()));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));

        QSqlQuery query(globalDb);
        query.prepare(QStringLiteral(
            "SELECT technical_summary, technical_summary IS NULL, source_text, source_text IS NULL, error_message, analysis_status "
            "FROM global_video_asset WHERE project_uuid = ?"));
        query.addBindValue(project.id);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString());
        QCOMPARE(query.value(1).toInt(), 0);
        QCOMPARE(query.value(2).toString(), QString());
        QCOMPARE(query.value(3).toInt(), 0);
        QCOMPARE(query.value(4).toString(), QString());
        QCOMPARE(query.value(5).toInt(), static_cast<int>(VideoAnalysisStatus::Pending));
        QVERIFY(!query.next());

        QSqlQuery downgradeStatus(globalDb);
        downgradeStatus.prepare(QStringLiteral(
            "UPDATE global_video_asset SET analysis_status = ? WHERE project_uuid = ?"));
        downgradeStatus.addBindValue(static_cast<int>(VideoAnalysisStatus::IndexedOnly));
        downgradeStatus.addBindValue(project.id);
        QVERIFY2(downgradeStatus.exec(), qPrintable(downgradeStatus.lastError().text()));
        QVERIFY2(rebuildProjectIntoGlobalForTest(globalDb,
                                                 project,
                                                 globalDatabaseManager.hasFts5(),
                                                 &errorMessage),
                 qPrintable(errorMessage));
        QSqlQuery upgradedStatus(globalDb);
        upgradedStatus.prepare(QStringLiteral(
            "SELECT analysis_status FROM global_video_asset WHERE project_uuid = ?"));
        upgradedStatus.addBindValue(project.id);
        QVERIFY2(upgradedStatus.exec(), qPrintable(upgradedStatus.lastError().text()));
        QVERIFY(upgradedStatus.next());
        QCOMPARE(upgradedStatus.value(0).toInt(), static_cast<int>(VideoAnalysisStatus::Pending));

        if (globalDatabaseManager.hasFts5()) {
            QSqlQuery ftsQuery(globalDb);
            ftsQuery.prepare(QStringLiteral("SELECT source_text FROM video_search_fts WHERE project_name = 'SyncProject'"));
            QVERIFY2(ftsQuery.exec(), qPrintable(ftsQuery.lastError().text()));
            QVERIFY(ftsQuery.next());
            QCOMPARE(ftsQuery.value(0).toString(), QString());
        }

        globalDatabaseManager.closeDatabase();
    }

    void syncCurrentProject_resolvesRealCaptureTimeFromProbeMetadata()
    {
        removeGlobalDatabaseFiles();

        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-capture-time");
        project.name = QStringLiteral("CaptureTimeProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-15T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage),
                 qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(sourcePath));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertBareVideoAsset(databaseManager.database(), sourceRootId, sourcePath, &errorMessage),
                 qPrintable(errorMessage));
        const auto assetId = firstAssetId(databaseManager.database(), &errorMessage);
        QVERIFY2(assetId > 0, qPrintable(errorMessage));

        QSqlQuery metadata(databaseManager.database());
        metadata.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO media_metadata "
            "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, raw_json, error_message, updated_at) "
            "VALUES (?, 2, 1, 'mov', 1000, 1000000, ?, '', '2026-07-15T10:01:00')"));
        metadata.addBindValue(assetId);
        metadata.addBindValue(QStringLiteral(
            R"({"format":{"tags":{"creation_time":"2026-07-12T01:00:00Z","com.apple.quicktime.creationdate":"2026-07-13T23:30:00+08:00"}}})"));
        QVERIFY2(metadata.exec(), qPrintable(metadata.lastError().text()));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb,
                                              project,
                                              globalDatabaseManager.hasFts5(),
                                              &errorMessage),
                 qPrintable(errorMessage));

        QSqlQuery capture(globalDb);
        QVERIFY2(capture.exec(QStringLiteral(
                     "SELECT capture_time, capture_date, capture_time_source, capture_time_confidence "
                     "FROM global_video_asset WHERE project_uuid = 'project-capture-time'")),
                 qPrintable(capture.lastError().text()));
        QVERIFY(capture.next());
        QCOMPARE(capture.value(1).toString(), QStringLiteral("2026-07-13"));
        QCOMPARE(capture.value(2).toString(), QStringLiteral("quicktime_creation_date"));
        QCOMPARE(capture.value(3).toDouble(), 1.0);
        QVERIFY(capture.value(0).toString().startsWith(QStringLiteral("2026-07-13T23:30:00")));

        QSqlQuery embedded(databaseManager.database());
        embedded.prepare(QStringLiteral(
            "INSERT INTO embedded_metadata "
            "(asset_id, status, capture_time, camera_make, camera_model, width, height, search_text, raw_json, updated_at) "
            "VALUES (?, ?, '2026-07-11T05:06:07+08:00', 'Sony', 'AlphaA7M4', 7680, 4320, "
            "'EXIF:Make Sony EXIF:Model AlphaA7M4', '{}', '2026-07-15T10:02:00')"));
        embedded.addBindValue(assetId);
        embedded.addBindValue(static_cast<int>(ProbeStatus::Success));
        QVERIFY2(embedded.exec(), qPrintable(embedded.lastError().text()));
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb,
                                              project,
                                              globalDatabaseManager.hasFts5(),
                                              &errorMessage),
                 qPrintable(errorMessage));

        QSqlQuery embeddedCapture(globalDb);
        embeddedCapture.prepare(QStringLiteral(
            "SELECT capture_time, capture_date, capture_time_source, capture_time_confidence, "
            "embedded_metadata_text, technical_summary FROM global_video_asset "
            "WHERE project_uuid = 'project-capture-time'"));
        QVERIFY2(embeddedCapture.exec(), qPrintable(embeddedCapture.lastError().text()));
        QVERIFY(embeddedCapture.next());
        QVERIFY(embeddedCapture.value(0).toString().startsWith(QStringLiteral("2026-07-11T05:06:07")));
        QCOMPARE(embeddedCapture.value(1).toString(), QStringLiteral("2026-07-11"));
        QCOMPARE(embeddedCapture.value(2).toString(), QStringLiteral("ExifTool"));
        QCOMPARE(embeddedCapture.value(3).toDouble(), 0.99);
        QVERIFY(embeddedCapture.value(4).toString().contains(QStringLiteral("AlphaA7M4")));
        QVERIFY(embeddedCapture.value(5).toString().contains(QStringLiteral("7680×4320")));

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

        QSqlQuery plan(globalDb);
        plan.prepare(QStringLiteral(
            "SELECT sampling_policy, frame_interval, source_frame_count, planned_frame_count "
            "FROM video_analysis_plan WHERE video_key = ?"));
        plan.addBindValue(newVideoKey);
        QVERIFY2(plan.exec(), qPrintable(plan.lastError().text()));
        QVERIFY(plan.next());
        QCOMPARE(plan.value(0).toString(), QStringLiteral("fixed_interval"));
        QCOMPARE(plan.value(1).toInt(), 10);
        QCOMPARE(plan.value(2).toInt(), 25);
        QCOMPARE(plan.value(3).toInt(), 3);

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

    void syncFolders_handlesRenameDeleteAndOrphanCleanup()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-folders");
        project.name = QStringLiteral("FolderProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-05T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA"))));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QString(), 0, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("2026-07-14"), 0, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("2026-07-14/CameraA"), 1, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertVideoAssetAt(databaseManager.database(), sourceRootId, sourcePath,
                                    QStringLiteral("2026-07-14/CameraA/clip.mov"), &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));

        const auto oldFolderKey = FolderPathMetadata::globalFolderKey(project.id,
                                                                      sourceRootId,
                                                                      QStringLiteral("2026-07-14/CameraA"));
        QSqlQuery initialFolders(globalDb);
        initialFolders.prepare(QStringLiteral(
            "SELECT COUNT(*), SUM(CASE WHEN normalized_date = '2026-07-14' THEN 1 ELSE 0 END) "
            "FROM global_folder_node WHERE project_uuid = ?"));
        initialFolders.addBindValue(project.id);
        QVERIFY2(initialFolders.exec(), qPrintable(initialFolders.lastError().text()));
        QVERIFY(initialFolders.next());
        QCOMPARE(initialFolders.value(0).toInt(), 3);
        QCOMPARE(initialFolders.value(1).toInt(), 2);

        QSqlQuery initialAsset(globalDb);
        initialAsset.prepare(QStringLiteral("SELECT folder_key, is_available FROM global_video_asset WHERE project_uuid = ?"));
        initialAsset.addBindValue(project.id);
        QVERIFY2(initialAsset.exec(), qPrintable(initialAsset.lastError().text()));
        QVERIFY(initialAsset.next());
        QCOMPARE(initialAsset.value(0).toString(), oldFolderKey);
        QCOMPARE(initialAsset.value(1).toInt(), 1);

        const auto renamedRelativePath = QStringLiteral("2026-07-14/CameraB");
        const auto renamedAbsolutePath = QDir(sourcePath).filePath(renamedRelativePath);
        QVERIFY(QDir().rename(QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA")), renamedAbsolutePath));
        QSqlQuery renameFolder(databaseManager.database());
        renameFolder.prepare(QStringLiteral(
            "UPDATE folder_node SET name = 'CameraB', absolute_path = ?, path_key = ?, relative_path = ? "
            "WHERE source_root_id = ? AND relative_path = '2026-07-14/CameraA'"));
        renameFolder.addBindValue(renamedAbsolutePath);
        renameFolder.addBindValue(FolderPathMetadata::normalizedPathKey(renamedAbsolutePath));
        renameFolder.addBindValue(renamedRelativePath);
        renameFolder.addBindValue(sourceRootId);
        QVERIFY2(renameFolder.exec(), qPrintable(renameFolder.lastError().text()));

        const auto renamedAssetPath = QDir(renamedAbsolutePath).filePath(QStringLiteral("clip.mov"));
        QSqlQuery renameAsset(databaseManager.database());
        renameAsset.prepare(QStringLiteral(
            "UPDATE asset_file SET absolute_path = ?, relative_path = ?, parent_path = ? WHERE source_root_id = ?"));
        renameAsset.addBindValue(renamedAssetPath);
        renameAsset.addBindValue(QStringLiteral("2026-07-14/CameraB/clip.mov"));
        renameAsset.addBindValue(renamedAbsolutePath);
        renameAsset.addBindValue(sourceRootId);
        QVERIFY2(renameAsset.exec(), qPrintable(renameAsset.lastError().text()));

        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        const auto newFolderKey = FolderPathMetadata::globalFolderKey(project.id, sourceRootId, renamedRelativePath);

        QSqlQuery renamedFolders(globalDb);
        renamedFolders.prepare(QStringLiteral(
            "SELECT SUM(CASE WHEN folder_key = ? THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN folder_key = ? THEN 1 ELSE 0 END) FROM global_folder_node WHERE project_uuid = ?"));
        renamedFolders.addBindValue(oldFolderKey);
        renamedFolders.addBindValue(newFolderKey);
        renamedFolders.addBindValue(project.id);
        QVERIFY2(renamedFolders.exec(), qPrintable(renamedFolders.lastError().text()));
        QVERIFY(renamedFolders.next());
        QCOMPARE(renamedFolders.value(0).toInt(), 0);
        QCOMPARE(renamedFolders.value(1).toInt(), 1);

        QSqlQuery renamedAsset(globalDb);
        renamedAsset.prepare(QStringLiteral("SELECT folder_key FROM global_video_asset WHERE project_uuid = ?"));
        renamedAsset.addBindValue(project.id);
        QVERIFY2(renamedAsset.exec(), qPrintable(renamedAsset.lastError().text()));
        QVERIFY(renamedAsset.next());
        QCOMPARE(renamedAsset.value(0).toString(), newFolderKey);

        QSqlQuery deleteProjectRows(databaseManager.database());
        QVERIFY2(deleteProjectRows.exec(QStringLiteral("DELETE FROM asset_file")), qPrintable(deleteProjectRows.lastError().text()));
        deleteProjectRows.prepare(QStringLiteral("DELETE FROM folder_node WHERE relative_path = ?"));
        deleteProjectRows.addBindValue(renamedRelativePath);
        QVERIFY2(deleteProjectRows.exec(), qPrintable(deleteProjectRows.lastError().text()));

        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        QSqlQuery cleanup(globalDb);
        cleanup.prepare(QStringLiteral(
            "SELECT (SELECT COUNT(*) FROM global_video_asset WHERE project_uuid = ?), "
            "(SELECT COUNT(*) FROM global_folder_node WHERE project_uuid = ?), "
            "(SELECT COUNT(*) FROM global_video_asset ga WHERE ga.project_uuid = ? AND ga.folder_key <> '' "
            "AND NOT EXISTS (SELECT 1 FROM global_folder_node gf WHERE gf.folder_key = ga.folder_key))"));
        cleanup.addBindValue(project.id);
        cleanup.addBindValue(project.id);
        cleanup.addBindValue(project.id);
        QVERIFY2(cleanup.exec(), qPrintable(cleanup.lastError().text()));
        QVERIFY(cleanup.next());
        QCOMPARE(cleanup.value(0).toInt(), 0);
        QCOMPARE(cleanup.value(1).toInt(), 2);
        QCOMPARE(cleanup.value(2).toInt(), 0);

        globalDatabaseManager.closeDatabase();
    }

    void syncFolders_preservesRowsWhenProjectDatabaseIsOffline()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-offline");
        project.name = QStringLiteral("OfflineProject");
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
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QString(), 1, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertVideoAssetAt(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("clip.mov"), &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));

        databaseManager.closeProjectDatabase();
        const auto offlinePath = project.databasePath + QStringLiteral(".offline");
        QVERIFY(QFile::rename(project.databasePath, offlinePath));
        QVERIFY(!syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage));
        QVERIFY(errorMessage.contains(QStringLiteral("离线")));

        QSqlQuery state(globalDb);
        state.prepare(QStringLiteral(
            "SELECT pr.sync_status, "
            "(SELECT COUNT(*) FROM global_folder_node gf WHERE gf.project_uuid = pr.project_uuid), "
            "(SELECT SUM(is_available) FROM global_folder_node gf WHERE gf.project_uuid = pr.project_uuid), "
            "(SELECT COUNT(*) FROM global_video_asset ga WHERE ga.project_uuid = pr.project_uuid), "
            "(SELECT SUM(is_available) FROM global_video_asset ga WHERE ga.project_uuid = pr.project_uuid) "
            "FROM project_registry pr WHERE pr.project_uuid = ?"));
        state.addBindValue(project.id);
        QVERIFY2(state.exec(), qPrintable(state.lastError().text()));
        QVERIFY(state.next());
        QCOMPARE(state.value(0).toString(), QStringLiteral("offline"));
        QCOMPARE(state.value(1).toInt(), 1);
        QCOMPARE(state.value(2).toInt(), 0);
        QCOMPARE(state.value(3).toInt(), 1);
        QCOMPARE(state.value(4).toInt(), 0);

        globalDatabaseManager.closeDatabase();
    }

    void deltaSync_updatesOnlyChangedAssetAndReportsThumbnailMask()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-delta-one");
        project.name = QStringLiteral("DeltaOneProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-21T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(sourcePath));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QString(), 3, 3, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertSyntheticAssets(databaseManager.database(), sourceRootId, sourcePath, 3, &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        const auto assetId = firstAssetId(databaseManager.database(), &errorMessage);
        QVERIFY(assetId > 0);

        QSqlQuery generation(globalDb);
        generation.prepare(QStringLiteral(
            "SELECT active_sync_generation FROM project_registry WHERE project_uuid = ?"));
        generation.addBindValue(project.id);
        QVERIFY2(generation.exec(), qPrintable(generation.lastError().text()));
        QVERIFY(generation.next());
        const auto activeGeneration = generation.value(0).toLongLong();

        QSqlQuery projectUpdate(databaseManager.database());
        projectUpdate.prepare(QStringLiteral(
            "UPDATE asset_file SET size_bytes = 999, modified_at = '2026-07-21T11:00:00' WHERE id = ?"));
        projectUpdate.addBindValue(assetId);
        QVERIFY2(projectUpdate.exec(), qPrintable(projectUpdate.lastError().text()));
        projectUpdate.prepare(QStringLiteral(
            "INSERT INTO thumbnail (asset_id, status, image_path, updated_at) "
            "VALUES (?, 1, 'thumb-delta.jpg', '2026-07-21T11:01:00')"));
        projectUpdate.addBindValue(assetId);
        QVERIFY2(projectUpdate.exec(), qPrintable(projectUpdate.lastError().text()));

        QSqlQuery rejectUnrelated(globalDb);
        QVERIFY2(rejectUnrelated.exec(QStringLiteral(
                     "CREATE TRIGGER reject_unrelated_delta BEFORE UPDATE ON global_video_asset "
                     "WHEN NEW.project_uuid = 'project-delta-one' AND NEW.asset_id != %1 "
                     "BEGIN SELECT RAISE(ABORT, 'delta touched unrelated asset'); END").arg(assetId)),
                 qPrintable(rejectUnrelated.lastError().text()));

        QVector<CatalogChangeSet> changeSets;
        QVERIFY2(syncProjectIntoGlobalWithDeltasForTest(globalDb,
                                                        project,
                                                        globalDatabaseManager.hasFts5(),
                                                        &changeSets,
                                                        &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(rejectUnrelated.exec(QStringLiteral("DROP TRIGGER reject_unrelated_delta")),
                 qPrintable(rejectUnrelated.lastError().text()));

        QCOMPARE(changeSets.size(), 1);
        QVERIFY(!changeSets.constFirst().fullRebuild);
        QCOMPARE(changeSets.constFirst().changes.size(), 1);
        const auto &change = changeSets.constFirst().changes.constFirst();
        QCOMPARE(change.entity, CatalogChangeEntity::Asset);
        QCOMPARE(change.entityId, assetId);
        QCOMPARE(change.operation, CatalogChangeOperation::Updated);
        QCOMPARE(change.changeMask,
                 CatalogChangeMask::AssetCore | CatalogChangeMask::Thumbnail);

        QSqlQuery globalAsset(globalDb);
        globalAsset.prepare(QStringLiteral(
            "SELECT size_bytes, modified_at, thumbnail_path, sync_generation "
            "FROM global_video_asset WHERE project_uuid = ? AND asset_id = ?"));
        globalAsset.addBindValue(project.id);
        globalAsset.addBindValue(assetId);
        QVERIFY2(globalAsset.exec(), qPrintable(globalAsset.lastError().text()));
        QVERIFY(globalAsset.next());
        QCOMPARE(globalAsset.value(0).toLongLong(), qint64{999});
        QCOMPARE(globalAsset.value(1).toString(), QStringLiteral("2026-07-21T11:00:00"));
        QCOMPARE(globalAsset.value(2).toString(), QStringLiteral("thumb-delta.jpg"));
        QCOMPARE(globalAsset.value(3).toLongLong(), activeGeneration);

        QSqlQuery cleared(databaseManager.database());
        QVERIFY2(cleared.exec(QStringLiteral("SELECT COUNT(*) FROM catalog_change_log")),
                 qPrintable(cleared.lastError().text()));
        QVERIFY(cleared.next());
        QCOMPARE(cleared.value(0).toInt(), 0);
        globalDatabaseManager.closeDatabase();
    }

    void deltaSync_emptyInitialProjectClearsMigrationRebuildFlag()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-delta-empty");
        project.name = QStringLiteral("DeltaEmptyProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-21T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        QSqlQuery state(databaseManager.database());
        QVERIFY2(state.exec(QStringLiteral(
                     "SELECT requires_full_rebuild, pending_change_count FROM catalog_change_state")),
                 qPrintable(state.lastError().text()));
        QVERIFY(state.next());
        QCOMPARE(state.value(0).toInt(), 0);
        QCOMPARE(state.value(1).toInt(), 0);

        QSqlQuery generation(globalDb);
        generation.prepare(QStringLiteral(
            "SELECT active_sync_generation FROM project_registry WHERE project_uuid = ?"));
        generation.addBindValue(project.id);
        QVERIFY2(generation.exec(), qPrintable(generation.lastError().text()));
        QVERIFY(generation.next());
        const auto activeGeneration = generation.value(0).toLongLong();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(generation.exec(), qPrintable(generation.lastError().text()));
        QVERIFY(generation.next());
        QCOMPARE(generation.value(0).toLongLong(), activeGeneration);
        globalDatabaseManager.closeDatabase();
    }

    void deltaSync_appliesAddedUpdatedRemovedAssetsAndFolders()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-delta-entities");
        project.name = QStringLiteral("DeltaEntitiesProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-21T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(sourcePath));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QString(), 0, 2, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("RenameMe"), 1, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("RemoveMe"), 1, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertVideoAssetAt(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("old.mov"), &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        const auto removedAssetId = firstAssetId(databaseManager.database(), &errorMessage);
        const auto removedVideoKey = QStringLiteral("%1:%2").arg(project.id).arg(removedAssetId);
        QSqlQuery analysis(globalDb);
        analysis.prepare(QStringLiteral(
            "INSERT INTO video_analysis_result "
            "(video_key, summary, keywords_json, scenes_json, search_text, model_name, prompt_version, "
            "analyzed_at, confirmed_at) VALUES (?, 'to remove', '[]', '[]', 'to remove', 'test', 'v1', "
            "'2026-07-21T10:30:00', '')"));
        analysis.addBindValue(removedVideoKey);
        QVERIFY2(analysis.exec(), qPrintable(analysis.lastError().text()));

        QSqlQuery mutate(databaseManager.database());
        mutate.prepare(QStringLiteral(
            "UPDATE folder_node SET name = 'Renamed', absolute_path = ?, path_key = ?, relative_path = 'Renamed' "
            "WHERE relative_path = 'RenameMe'"));
        const auto renamedPath = QDir(sourcePath).filePath(QStringLiteral("Renamed"));
        mutate.addBindValue(renamedPath);
        mutate.addBindValue(FolderPathMetadata::normalizedPathKey(renamedPath));
        QVERIFY2(mutate.exec(), qPrintable(mutate.lastError().text()));
        QVERIFY2(mutate.exec(QStringLiteral("DELETE FROM folder_node WHERE relative_path = 'RemoveMe'")),
                 qPrintable(mutate.lastError().text()));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("Added"), 1, 1, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(mutate.exec(QStringLiteral("DELETE FROM asset_file WHERE relative_path = 'old.mov'")),
                 qPrintable(mutate.lastError().text()));
        QVERIFY2(insertVideoAssetAt(databaseManager.database(), sourceRootId, sourcePath, QStringLiteral("Added/new.mov"), &errorMessage),
                 qPrintable(errorMessage));

        QVector<CatalogChangeSet> changeSets;
        QVERIFY2(syncProjectIntoGlobalWithDeltasForTest(globalDb,
                                                        project,
                                                        globalDatabaseManager.hasFts5(),
                                                        &changeSets,
                                                        &errorMessage),
                 qPrintable(errorMessage));
        int totalChanges = 0;
        bool assetAdded = false;
        bool assetRemoved = false;
        bool folderAdded = false;
        bool folderUpdated = false;
        bool folderRemoved = false;
        for (const auto &changeSet : changeSets) {
            QVERIFY(changeSet.changes.size() <= 500);
            totalChanges += changeSet.changes.size();
            for (const auto &change : changeSet.changes) {
                if (change.entity == CatalogChangeEntity::Asset) {
                    assetAdded |= change.operation == CatalogChangeOperation::Added;
                    assetRemoved |= change.operation == CatalogChangeOperation::Removed;
                } else {
                    folderAdded |= change.operation == CatalogChangeOperation::Added;
                    folderUpdated |= change.operation == CatalogChangeOperation::Updated;
                    folderRemoved |= change.operation == CatalogChangeOperation::Removed;
                }
            }
        }
        QCOMPARE(totalChanges, 5);
        QVERIFY(assetAdded);
        QVERIFY(assetRemoved);
        QVERIFY(folderAdded);
        QVERIFY(folderUpdated);
        QVERIFY(folderRemoved);

        QSqlQuery assets(globalDb);
        assets.prepare(QStringLiteral(
            "SELECT SUM(CASE WHEN asset_id = ? THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN relative_path = 'Added/new.mov' THEN 1 ELSE 0 END) "
            "FROM global_video_asset WHERE project_uuid = ?"));
        assets.addBindValue(removedAssetId);
        assets.addBindValue(project.id);
        QVERIFY2(assets.exec(), qPrintable(assets.lastError().text()));
        QVERIFY(assets.next());
        QCOMPARE(assets.value(0).toInt(), 0);
        QCOMPARE(assets.value(1).toInt(), 1);
        analysis.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM video_analysis_result WHERE video_key = ?"));
        analysis.addBindValue(removedVideoKey);
        QVERIFY2(analysis.exec(), qPrintable(analysis.lastError().text()));
        QVERIFY(analysis.next());
        QCOMPARE(analysis.value(0).toInt(), 0);

        const auto renamedFolderKey = FolderPathMetadata::globalFolderKey(project.id, sourceRootId, QStringLiteral("Renamed"));
        const auto removedFolderKey = FolderPathMetadata::globalFolderKey(project.id, sourceRootId, QStringLiteral("RemoveMe"));
        const auto addedFolderKey = FolderPathMetadata::globalFolderKey(project.id, sourceRootId, QStringLiteral("Added"));
        QSqlQuery folders(globalDb);
        folders.prepare(QStringLiteral(
            "SELECT SUM(CASE WHEN folder_key = ? THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN folder_key = ? THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN folder_key = ? THEN 1 ELSE 0 END) "
            "FROM global_folder_node WHERE project_uuid = ?"));
        folders.addBindValue(renamedFolderKey);
        folders.addBindValue(removedFolderKey);
        folders.addBindValue(addedFolderKey);
        folders.addBindValue(project.id);
        QVERIFY2(folders.exec(), qPrintable(folders.lastError().text()));
        QVERIFY(folders.next());
        QCOMPARE(folders.value(0).toInt(), 1);
        QCOMPARE(folders.value(1).toInt(), 0);
        QCOMPARE(folders.value(2).toInt(), 1);
        globalDatabaseManager.closeDatabase();
    }

    void deltaSync_secondPageFailureKeepsWatermarkAndRetryDoesNotMissChanges()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-delta-retry");
        project.name = QStringLiteral("DeltaRetryProject");
        project.rootPath = QDir(temp.path()).filePath(project.name);
        project.databasePath = QDir(project.rootPath).filePath(QStringLiteral("project.cvdb"));
        project.createdAt = QStringLiteral("2026-07-21T10:00:00");
        QVERIFY(QDir().mkpath(project.rootPath));
        QVERIFY2(databaseManager.openProjectDatabase(project.databasePath, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(insertProjectRecord(databaseManager.database(), project, &errorMessage), qPrintable(errorMessage));

        const auto sourcePath = QDir(project.rootPath).filePath(QStringLiteral("Source"));
        QVERIFY(QDir().mkpath(sourcePath));
        qint64 sourceRootId = 0;
        QVERIFY2(insertSourceRoot(databaseManager.database(), sourcePath, &sourceRootId, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QString(), 1205, 1205, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertSyntheticAssets(databaseManager.database(), sourceRootId, sourcePath, 1205, &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        QSqlQuery updateAll(databaseManager.database());
        QVERIFY2(updateAll.exec(QStringLiteral(
                     "UPDATE asset_file SET size_bytes = size_bytes + 1, modified_at = '2026-07-21T12:00:00'")),
                 qPrintable(updateAll.lastError().text()));

        QSqlQuery failSecondPage(globalDb);
        QVERIFY2(failSecondPage.exec(QStringLiteral(
                     "CREATE TRIGGER fail_delta_second_page BEFORE UPDATE ON global_video_asset "
                     "WHEN NEW.project_uuid = 'project-delta-retry' AND NEW.asset_id > 500 "
                     "BEGIN SELECT RAISE(ABORT, 'delta second page failure'); END")),
                 qPrintable(failSecondPage.lastError().text()));
        QVERIFY(!syncProjectIntoGlobalForTest(
            globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage));
        QVERIFY(errorMessage.contains(QStringLiteral("delta second page failure")));

        QSqlQuery retained(databaseManager.database());
        QVERIFY2(retained.exec(QStringLiteral("SELECT COUNT(*) FROM catalog_change_log")),
                 qPrintable(retained.lastError().text()));
        QVERIFY(retained.next());
        QCOMPARE(retained.value(0).toInt(), 1205);

        QVERIFY2(failSecondPage.exec(QStringLiteral("DROP TRIGGER fail_delta_second_page")),
                 qPrintable(failSecondPage.lastError().text()));
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(retained.exec(QStringLiteral("SELECT COUNT(*) FROM catalog_change_log")),
                 qPrintable(retained.lastError().text()));
        QVERIFY(retained.next());
        QCOMPARE(retained.value(0).toInt(), 0);

        QSqlQuery complete(globalDb);
        complete.prepare(QStringLiteral(
            "SELECT COUNT(*), SUM(CASE WHEN modified_at = '2026-07-21T12:00:00' THEN 1 ELSE 0 END) "
            "FROM global_video_asset WHERE project_uuid = ?"));
        complete.addBindValue(project.id);
        QVERIFY2(complete.exec(), qPrintable(complete.lastError().text()));
        QVERIFY(complete.next());
        QCOMPARE(complete.value(0).toInt(), 1205);
        QCOMPARE(complete.value(1).toInt(), 1205);
        globalDatabaseManager.closeDatabase();
    }

    void generationSync_pagesLargeProjectsAndKeepsCompletedGenerationOnFailure()
    {
        removeGlobalDatabaseFiles();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DatabaseManager databaseManager;
        GlobalDatabaseManager globalDatabaseManager;
        QString errorMessage;
        QVERIFY2(globalDatabaseManager.openDatabase(&errorMessage), qPrintable(errorMessage));

        Project project;
        project.id = QStringLiteral("project-generation");
        project.name = QStringLiteral("GenerationProject");
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
        QVERIFY2(insertFolderNode(databaseManager.database(), sourceRootId, sourcePath, QString(), 1205, 1205, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(insertSyntheticAssets(databaseManager.database(), sourceRootId, sourcePath, 1205, &errorMessage),
                 qPrintable(errorMessage));

        auto globalDb = globalDatabaseManager.database();
        QVERIFY2(syncProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));

        QSqlQuery initial(globalDb);
        initial.prepare(QStringLiteral(
            "SELECT pr.active_sync_generation, pr.sync_status, COUNT(a.video_key), "
            "COUNT(DISTINCT a.sync_generation), MIN(a.sync_generation), MAX(a.sync_generation) "
            "FROM project_registry pr JOIN global_video_asset a ON a.project_uuid = pr.project_uuid "
            "WHERE pr.project_uuid = ? GROUP BY pr.project_uuid"));
        initial.addBindValue(project.id);
        QVERIFY2(initial.exec(), qPrintable(initial.lastError().text()));
        QVERIFY(initial.next());
        const auto completedGeneration = initial.value(0).toLongLong();
        QVERIFY(completedGeneration > 0);
        QCOMPARE(initial.value(1).toString(), QStringLiteral("ok"));
        QCOMPARE(initial.value(2).toInt(), 1205);
        QCOMPARE(initial.value(3).toInt(), 1);
        QCOMPARE(initial.value(4).toLongLong(), completedGeneration);
        QCOMPARE(initial.value(5).toLongLong(), completedGeneration);

        QSqlQuery failSecondPage(globalDb);
        QVERIFY2(failSecondPage.exec(QStringLiteral(
                     "CREATE TRIGGER fail_generation_second_page BEFORE UPDATE ON global_video_asset "
                     "WHEN NEW.asset_id > 500 BEGIN SELECT RAISE(ABORT, 'second page failure'); END")),
                 qPrintable(failSecondPage.lastError().text()));
        QVERIFY(!rebuildProjectIntoGlobalForTest(
            globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage));
        QVERIFY(errorMessage.contains(QStringLiteral("second page failure")));

        QSqlQuery failedState(globalDb);
        failedState.prepare(QStringLiteral(
            "SELECT active_sync_generation, sync_status, "
            "(SELECT COUNT(*) FROM global_video_asset a WHERE a.project_uuid = ? "
            "AND a.sync_generation != project_registry.active_sync_generation) "
            "FROM project_registry WHERE project_uuid = ?"));
        failedState.addBindValue(project.id);
        failedState.addBindValue(project.id);
        QVERIFY2(failedState.exec(), qPrintable(failedState.lastError().text()));
        QVERIFY(failedState.next());
        QCOMPARE(failedState.value(0).toLongLong(), completedGeneration);
        QCOMPARE(failedState.value(1).toString(), QStringLiteral("failed"));
        QCOMPARE(failedState.value(2).toInt(), 500);

        QVERIFY2(failSecondPage.exec(QStringLiteral("DROP TRIGGER fail_generation_second_page")),
                 qPrintable(failSecondPage.lastError().text()));
        QVERIFY2(rebuildProjectIntoGlobalForTest(globalDb, project, globalDatabaseManager.hasFts5(), &errorMessage),
                 qPrintable(errorMessage));
        QSqlQuery recovered(globalDb);
        recovered.prepare(QStringLiteral(
            "SELECT pr.active_sync_generation, pr.sync_status, COUNT(a.video_key), "
            "COUNT(DISTINCT a.sync_generation), MIN(a.sync_generation) "
            "FROM project_registry pr JOIN global_video_asset a ON a.project_uuid = pr.project_uuid "
            "WHERE pr.project_uuid = ? GROUP BY pr.project_uuid"));
        recovered.addBindValue(project.id);
        QVERIFY2(recovered.exec(), qPrintable(recovered.lastError().text()));
        QVERIFY(recovered.next());
        QVERIFY(recovered.value(0).toLongLong() > completedGeneration);
        QCOMPARE(recovered.value(1).toString(), QStringLiteral("ok"));
        QCOMPARE(recovered.value(2).toInt(), 1205);
        QCOMPARE(recovered.value(3).toInt(), 1);
        QCOMPARE(recovered.value(4).toLongLong(), recovered.value(0).toLongLong());

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
            QCOMPARE(version.value(0).toInt(), GlobalDatabaseManager::CurrentSchemaVersion);

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
