#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/db/DatabaseMigration.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

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
    QFile::remove(DatabaseMigration::backupFilePath(path, 8));
    QFile::remove(DatabaseMigration::backupFilePath(path, 9));
    QFile::remove(DatabaseMigration::backupFilePath(path, 11));
    QFile::remove(DatabaseMigration::backupFilePath(path, GlobalDatabaseManager::CurrentSchemaVersion));
    QFile::remove(DatabaseMigration::legacyMigrationMarkerPath(path));
    QFile::remove(path + QStringLiteral(".legacy-migration-tmp"));
}

bool executeStatements(QSqlDatabase &db, const QStringList &statements, QString *errorMessage)
{
    QSqlQuery query(db);
    for (const auto &statement : statements) {
        if (!query.exec(statement)) {
            if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }
    return true;
}

bool createLegacyProjectDatabase(const QString &databasePath,
                                 const QString &sourcePath,
                                 bool brokenFolderTable,
                                 QString *errorMessage)
{
    const auto connectionName = QStringLiteral("legacy_project_%1").arg(reinterpret_cast<quintptr>(&databasePath));
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(databasePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    QStringList statements = {
        QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)"),
        QStringLiteral("INSERT INTO schema_version VALUES (2)")
    };
    if (brokenFolderTable) {
        statements.append(QStringLiteral("CREATE TABLE folder_node (id INTEGER PRIMARY KEY)"));
    } else {
        statements.append({
            QStringLiteral("CREATE TABLE source_root ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, path TEXT NOT NULL UNIQUE, status TEXT NOT NULL, "
                           "total_files INTEGER NOT NULL DEFAULT 0, total_folders INTEGER NOT NULL DEFAULT 0, total_size_bytes INTEGER NOT NULL DEFAULT 0, "
                           "video_count INTEGER NOT NULL DEFAULT 0, audio_count INTEGER NOT NULL DEFAULT 0, image_count INTEGER NOT NULL DEFAULT 0, "
                           "other_count INTEGER NOT NULL DEFAULT 0, warning_count INTEGER NOT NULL DEFAULT 0, scan_version INTEGER NOT NULL DEFAULT 0, "
                           "created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE folder_node ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, source_root_id INTEGER NOT NULL, absolute_path TEXT NOT NULL, "
                           "relative_path TEXT NOT NULL, file_count INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE asset_file ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT, source_root_id INTEGER NOT NULL, name TEXT NOT NULL, extension TEXT, "
                           "absolute_path TEXT NOT NULL, relative_path TEXT NOT NULL, parent_path TEXT NOT NULL, asset_type INTEGER NOT NULL, "
                           "size_bytes INTEGER NOT NULL DEFAULT 0, modified_at TEXT NOT NULL, is_readable INTEGER NOT NULL DEFAULT 0, "
                           "is_favorite INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)"),
            QStringLiteral("INSERT INTO source_root "
                           "(id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
                           "other_count, warning_count, scan_version, created_at, updated_at) "
                           "VALUES (1, 'Card', '%1', 'ok', 3, 2, 30, 3, 0, 0, 0, 0, 2, '2026-07-14T10:00:00', '2026-07-14T10:00:00')")
                .arg(QString(sourcePath).replace(QLatin1Char('\''), QStringLiteral("''"))),
            QStringLiteral("INSERT INTO folder_node (source_root_id, absolute_path, relative_path, file_count, created_at) VALUES "
                           "(1, '%1', '2026-07-14', 0, '2026-07-14T10:00:00'), "
                           "(1, '%2', '2026-07-14/CameraA', 0, '2026-07-14T10:00:00')")
                .arg(QDir(sourcePath).filePath(QStringLiteral("2026-07-14")).replace(QLatin1Char('\''), QStringLiteral("''")),
                     QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA")).replace(QLatin1Char('\''), QStringLiteral("''"))),
            QStringLiteral("INSERT INTO asset_file "
                           "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, size_bytes, modified_at, is_readable, created_at) VALUES "
                           "(1, 'root.mov', 'mov', 'root.mov', 'root.mov', '%1', 1, 10, '2026-07-14T10:00:00', 1, '2026-07-14T10:00:00'), "
                           "(1, 'day.mov', 'mov', 'day.mov', '2026-07-14/day.mov', '%2', 1, 10, '2026-07-14T10:00:00', 1, '2026-07-14T10:00:00'), "
                           "(1, 'cam.mov', 'mov', 'cam.mov', '2026-07-14/CameraA/cam.mov', '%3', 1, 10, '2026-07-14T10:00:00', 1, '2026-07-14T10:00:00')")
                .arg(QString(sourcePath).replace(QLatin1Char('\''), QStringLiteral("''")),
                     QDir(sourcePath).filePath(QStringLiteral("2026-07-14")).replace(QLatin1Char('\''), QStringLiteral("''")),
                     QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA")).replace(QLatin1Char('\''), QStringLiteral("''")))
        });
    }

    const auto success = executeStatements(db, statements, errorMessage);
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return success;
}

int schemaVersionFromFile(const QString &databasePath, QString *errorMessage)
{
    const auto connectionName = QStringLiteral("read_version_%1").arg(reinterpret_cast<quintptr>(&databasePath));
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(databasePath);
    int version = -1;
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
    } else {
        QSqlQuery query(db);
        if (query.exec(QStringLiteral("SELECT version FROM schema_version")) && query.next()) {
            version = query.value(0).toInt();
        } else if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return version;
}

bool createRecoveryCandidate(const QString &databasePath,
                             int assetCount,
                             int frameCount,
                             QString *errorMessage)
{
    if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建测试数据库目录");
        }
        return false;
    }
    QFile::remove(databasePath);
    const auto connectionName = QStringLiteral("recovery_candidate_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool success = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
        } else {
            success = executeStatements(
                db,
                {QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)"),
                 QStringLiteral("INSERT INTO schema_version VALUES (11)"),
                 QStringLiteral("CREATE TABLE global_video_asset (video_key TEXT PRIMARY KEY)"),
                 QStringLiteral("CREATE TABLE video_analysis_result (video_key TEXT PRIMARY KEY)"),
                 QStringLiteral("CREATE TABLE video_frame_analysis (video_key TEXT, frame_number INTEGER)")},
                errorMessage);
            if (success) {
                QSqlQuery asset(db);
                asset.prepare(QStringLiteral("INSERT INTO global_video_asset(video_key) VALUES (?)"));
                for (int index = 0; index < assetCount && success; ++index) {
                    asset.addBindValue(QStringLiteral("asset-%1").arg(index));
                    success = asset.exec();
                    if (!success && errorMessage) {
                        *errorMessage = asset.lastError().text();
                    }
                }
            }
            if (success) {
                QSqlQuery frame(db);
                frame.prepare(QStringLiteral(
                    "INSERT INTO video_frame_analysis(video_key, frame_number) VALUES (?, ?)"));
                for (int index = 0; index < frameCount && success; ++index) {
                    frame.addBindValue(QStringLiteral("asset-%1").arg(index % qMax(1, assetCount)));
                    frame.addBindValue(index);
                    success = frame.exec();
                    if (!success && errorMessage) {
                        *errorMessage = frame.lastError().text();
                    }
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return success;
}

qint64 recoveryRowCount(const QString &databasePath, const QString &tableName, QString *errorMessage)
{
    const auto connectionName = QStringLiteral("recovery_count_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    qint64 count = -1;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
        } else {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT COUNT(*) FROM \"%1\"").arg(tableName)) && query.next()) {
                count = query.value(0).toLongLong();
            } else if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return count;
}
}

class FolderDatabaseMigrationTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        removeGlobalDatabaseFiles();
    }

    void globalUserData_isSeparatedFromInstallerAndRecoversPopulatedBackup()
    {
        QVERIFY(QDir::cleanPath(Paths::resolvedDataRoot()).compare(
                    QDir::cleanPath(Paths::installDataRoot()),
                    Qt::CaseInsensitive)
                != 0);

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto legacyRoot = QDir(temp.path()).filePath(QStringLiteral("legacy-install-data"));
        const auto userRoot = QDir(temp.path()).filePath(QStringLiteral("user-data"));
        QVERIFY(QDir().mkpath(legacyRoot));
        QVERIFY(QDir().mkpath(userRoot));

        const auto legacyMain = QDir(legacyRoot).filePath(QStringLiteral("material-center.sqlite"));
        const auto legacyBackup = DatabaseMigration::backupFilePath(legacyMain, 11);
        const auto destination = QDir(userRoot).filePath(QStringLiteral("material-center.sqlite"));
        QString errorMessage;
        QVERIFY2(createRecoveryCandidate(legacyMain, 0, 0, &errorMessage), qPrintable(errorMessage));
        QVERIFY2(createRecoveryCandidate(legacyBackup, 2, 5, &errorMessage), qPrintable(errorMessage));

        QString recoveryMessage;
        QVERIFY2(DatabaseMigration::ensureUserDatabase(destination,
                                                       legacyRoot,
                                                       &recoveryMessage,
                                                       &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY(recoveryMessage.contains(legacyBackup));
        QCOMPARE(recoveryRowCount(destination, QStringLiteral("global_video_asset"), &errorMessage), 2);
        QCOMPARE(recoveryRowCount(destination, QStringLiteral("video_frame_analysis"), &errorMessage), 5);
        QVERIFY(QFileInfo::exists(DatabaseMigration::legacyMigrationMarkerPath(destination)));

        const auto connectionName = QStringLiteral("clear_recovered_database");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(destination);
            QVERIFY2(db.open(), qPrintable(db.lastError().text()));
            QVERIFY2(executeStatements(db,
                                       {QStringLiteral("DELETE FROM video_frame_analysis"),
                                        QStringLiteral("DELETE FROM global_video_asset")},
                                       &errorMessage),
                     qPrintable(errorMessage));
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        recoveryMessage.clear();
        QVERIFY2(DatabaseMigration::ensureUserDatabase(destination,
                                                       legacyRoot,
                                                       &recoveryMessage,
                                                       &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY(recoveryMessage.isEmpty());
        QCOMPARE(recoveryRowCount(destination, QStringLiteral("global_video_asset"), &errorMessage), 0);
    }

    void projectV2ToV3_createsBackupAndBackfillsHierarchy()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto databasePath = QDir(temp.path()).filePath(QStringLiteral("project.cvdb"));
        const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("Card"));
        QVERIFY(QDir().mkpath(QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA"))));

        QString errorMessage;
        QVERIFY2(createLegacyProjectDatabase(databasePath, sourcePath, false, &errorMessage), qPrintable(errorMessage));

        DatabaseManager manager;
        QVERIFY2(manager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(manager.schemaVersion(), DatabaseManager::CurrentSchemaVersion);

        const auto backupPath = DatabaseMigration::backupFilePath(databasePath,
                                                                  DatabaseManager::CurrentSchemaVersion);
        QVERIFY(QFileInfo(backupPath).isFile());
        QVERIFY(QFileInfo(backupPath).size() > 0);
        QCOMPARE(schemaVersionFromFile(backupPath, &errorMessage), 2);

        QSqlQuery folders(manager.database());
        QVERIFY2(folders.exec(QStringLiteral(
                     "SELECT relative_path, parent_relative_path, depth, direct_file_count, recursive_file_count, "
                     "normalized_date, date_anchor, path_key FROM folder_node ORDER BY depth, relative_path")),
                 qPrintable(folders.lastError().text()));

        QVERIFY(folders.next());
        QCOMPARE(folders.value(0).toString(), QString());
        QCOMPARE(folders.value(1).toString(), QString());
        QCOMPARE(folders.value(2).toInt(), 0);
        QCOMPARE(folders.value(3).toLongLong(), qint64{1});
        QCOMPARE(folders.value(4).toLongLong(), qint64{3});
        QVERIFY(!folders.value(7).toString().isEmpty());

        QVERIFY(folders.next());
        QCOMPARE(folders.value(0).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(folders.value(2).toInt(), 1);
        QCOMPARE(folders.value(3).toLongLong(), qint64{1});
        QCOMPARE(folders.value(4).toLongLong(), qint64{2});
        QCOMPARE(folders.value(5).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(folders.value(6).toString(), QStringLiteral("2026-07-14"));

        QVERIFY(folders.next());
        QCOMPARE(folders.value(0).toString(), QStringLiteral("2026-07-14/CameraA"));
        QCOMPARE(folders.value(1).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(folders.value(2).toInt(), 2);
        QCOMPARE(folders.value(3).toLongLong(), qint64{1});
        QCOMPARE(folders.value(4).toLongLong(), qint64{1});
        QCOMPARE(folders.value(5).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(folders.value(6).toString(), QStringLiteral("2026-07-14"));
        QVERIFY(!folders.next());

        manager.closeProjectDatabase();
    }

    void projectV2ToV3_rollsBackAllSchemaChangesOnFailure()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto databasePath = QDir(temp.path()).filePath(QStringLiteral("broken.cvdb"));
        QString errorMessage;
        QVERIFY2(createLegacyProjectDatabase(databasePath, temp.path(), true, &errorMessage), qPrintable(errorMessage));

        DatabaseManager manager;
        QVERIFY(!manager.openProjectDatabase(databasePath, &errorMessage));
        QVERIFY(!errorMessage.isEmpty());
        QCOMPARE(schemaVersionFromFile(databasePath, &errorMessage), 2);
        QCOMPARE(schemaVersionFromFile(DatabaseMigration::backupFilePath(
                     databasePath,
                     DatabaseManager::CurrentSchemaVersion),
                 &errorMessage),
                 2);
    }

    void projectMetadataRetryColumns_repairFromV9AndCurrentSchema()
    {
        const QVector<int> legacyVersions{9, DatabaseManager::CurrentSchemaVersion};
        for (const auto legacyVersion : legacyVersions) {
            QTemporaryDir temp;
            QVERIFY(temp.isValid());
            const auto databasePath = QDir(temp.path()).filePath(
                QStringLiteral("metadata-retry-v%1.cvdb").arg(legacyVersion));
            const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("Card"));
            QVERIFY(QDir().mkpath(QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA"))));

            QString errorMessage;
            QVERIFY2(createLegacyProjectDatabase(databasePath, sourcePath, false, &errorMessage),
                     qPrintable(errorMessage));

            const auto connectionName = QStringLiteral("legacy_metadata_retry_%1")
                                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            {
                auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
                db.setDatabaseName(databasePath);
                QVERIFY2(db.open(), qPrintable(db.lastError().text()));
                const QStringList legacyMetadataSchema{
                    QStringLiteral("CREATE TABLE media_metadata ("
                                   "asset_id INTEGER PRIMARY KEY, "
                                   "probe_status INTEGER NOT NULL DEFAULT 0, "
                                   "media_type INTEGER NOT NULL DEFAULT 0, "
                                   "container TEXT, "
                                   "duration_ms INTEGER NOT NULL DEFAULT 0, "
                                   "bit_rate INTEGER NOT NULL DEFAULT 0, "
                                   "raw_json TEXT, "
                                   "error_message TEXT, "
                                   "updated_at TEXT NOT NULL)"),
                    QStringLiteral("INSERT INTO media_metadata "
                                   "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, "
                                   "raw_json, error_message, updated_at) "
                                   "VALUES (1, 1, 1, 'mov', 42000, 8000, '{\"format\":\"mov\"}', '', "
                                   "'2026-07-30T10:00:00')"),
                    QStringLiteral("CREATE TABLE embedded_metadata ("
                                   "asset_id INTEGER PRIMARY KEY, "
                                   "status INTEGER NOT NULL DEFAULT 0, "
                                   "capture_time TEXT NOT NULL DEFAULT '', "
                                   "camera_make TEXT NOT NULL DEFAULT '', "
                                   "camera_model TEXT NOT NULL DEFAULT '', "
                                   "lens_model TEXT NOT NULL DEFAULT '', "
                                   "search_text TEXT NOT NULL DEFAULT '', "
                                   "raw_json TEXT NOT NULL DEFAULT '', "
                                   "error_message TEXT NOT NULL DEFAULT '', "
                                   "updated_at TEXT NOT NULL DEFAULT '')"),
                    QStringLiteral("INSERT INTO embedded_metadata "
                                   "(asset_id, status, capture_time, camera_make, camera_model, lens_model, "
                                   "search_text, raw_json, error_message, updated_at) "
                                   "VALUES (1, 1, '2026-07-30T09:59:00', 'CameraCo', 'ModelA', 'LensA', "
                                   "'camera metadata', '{\"make\":\"CameraCo\"}', '', '2026-07-30T10:01:00')"),
                    QStringLiteral("UPDATE schema_version SET version = %1").arg(legacyVersion)
                };
                QVERIFY2(executeStatements(db, legacyMetadataSchema, &errorMessage),
                         qPrintable(errorMessage));
                db.close();
            }
            QSqlDatabase::removeDatabase(connectionName);

            DatabaseManager manager;
            QVERIFY2(manager.openProjectDatabase(databasePath, &errorMessage),
                     qPrintable(QStringLiteral("legacy version %1: %2")
                                    .arg(legacyVersion)
                                    .arg(errorMessage)));
            QCOMPARE(manager.schemaVersion(), DatabaseManager::CurrentSchemaVersion);

            QSqlQuery columns(manager.database());
            QVERIFY2(columns.exec(QStringLiteral("PRAGMA table_info(media_metadata)")),
                     qPrintable(columns.lastError().text()));
            QStringList columnNames;
            while (columns.next()) {
                columnNames.append(columns.value(1).toString());
            }
            const QStringList requiredColumns{
                QStringLiteral("retry_count"),
                QStringLiteral("next_retry_at"),
                QStringLiteral("retry_policy_version"),
                QStringLiteral("failure_kind"),
                QStringLiteral("last_attempt_at")
            };
            for (const auto &column : requiredColumns) {
                QVERIFY2(columnNames.contains(column), qPrintable(column));
            }

            QSqlQuery metadata(manager.database());
            QVERIFY2(metadata.exec(QStringLiteral(
                         "SELECT probe_status, retry_count, next_retry_at, retry_policy_version, "
                         "failure_kind, last_attempt_at, updated_at, raw_json "
                         "FROM media_metadata WHERE asset_id = 1")),
                     qPrintable(metadata.lastError().text()));
            QVERIFY(metadata.next());
            QCOMPARE(metadata.value(0).toInt(), 1);
            QCOMPARE(metadata.value(1).toInt(), 0);
            QCOMPARE(metadata.value(2).toString(), QString());
            QCOMPARE(metadata.value(3).toInt(), 0);
            QCOMPARE(metadata.value(4).toString(), QString());
            QCOMPARE(metadata.value(5).toString(), QString());
            QCOMPARE(metadata.value(6).toString(), QStringLiteral("2026-07-30T10:00:00"));
            QCOMPARE(metadata.value(7).toString(), QStringLiteral("{\"format\":\"mov\"}"));

            QSqlQuery embeddedColumns(manager.database());
            QVERIFY2(embeddedColumns.exec(QStringLiteral("PRAGMA table_info(embedded_metadata)")),
                     qPrintable(embeddedColumns.lastError().text()));
            QStringList embeddedColumnNames;
            while (embeddedColumns.next()) {
                embeddedColumnNames.append(embeddedColumns.value(1).toString());
            }
            for (const auto &column : requiredColumns) {
                QVERIFY2(embeddedColumnNames.contains(column), qPrintable(column));
            }

            QSqlQuery embedded(manager.database());
            QVERIFY2(embedded.exec(QStringLiteral(
                         "SELECT status, retry_count, next_retry_at, retry_policy_version, "
                         "failure_kind, last_attempt_at, raw_json "
                         "FROM embedded_metadata WHERE asset_id = 1")),
                     qPrintable(embedded.lastError().text()));
            QVERIFY(embedded.next());
            QCOMPARE(embedded.value(0).toInt(), 1);
            QCOMPARE(embedded.value(1).toInt(), 0);
            QCOMPARE(embedded.value(2).toString(), QString());
            QCOMPARE(embedded.value(3).toInt(), 0);
            QCOMPARE(embedded.value(4).toString(), QString());
            QCOMPARE(embedded.value(5).toString(), QString());
            QCOMPARE(embedded.value(6).toString(), QStringLiteral("{\"make\":\"CameraCo\"}"));

            QSqlQuery index(manager.database());
            QVERIFY2(index.exec(QStringLiteral(
                         "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                         "AND name IN ('idx_media_metadata_retry', 'idx_embedded_metadata_retry', "
                         "'idx_thumbnail_retry_schedule')")),
                     qPrintable(index.lastError().text()));
            QVERIFY(index.next());
            QCOMPARE(index.value(0).toInt(), 3);
            manager.closeProjectDatabase();
        }
    }

    void projectV8_changeLogCoalescesEntityUpdatesAndRepairsTriggers()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto databasePath = QDir(temp.path()).filePath(QStringLiteral("catalog-v8.cvdb"));

        DatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
        QCOMPARE(manager.schemaVersion(), DatabaseManager::CurrentSchemaVersion);

        auto db = manager.database();
        QSqlQuery write(db);
        QVERIFY2(write.exec(QStringLiteral(
                     "INSERT INTO source_root "
                     "(name, path, status, created_at, updated_at) "
                     "VALUES ('Card', 'C:/Card', 'ok', '2026-07-21T10:00:00', '2026-07-21T10:00:00')")),
                 qPrintable(write.lastError().text()));
        const auto sourceRootId = write.lastInsertId().toLongLong();
        write.prepare(QStringLiteral(
            "INSERT INTO folder_node "
            "(source_root_id, name, absolute_path, relative_path, created_at) "
            "VALUES (?, 'Card', 'C:/Card', '', '2026-07-21T10:00:00')"));
        write.addBindValue(sourceRootId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));
        const auto folderId = write.lastInsertId().toLongLong();
        write.prepare(QStringLiteral(
            "INSERT INTO asset_file "
            "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, "
            "size_bytes, modified_at, is_readable, created_at) "
            "VALUES (?, 'clip.mov', 'mov', 'C:/Card/clip.mov', 'clip.mov', 'C:/Card', 1, 10, "
            "'2026-07-21T10:00:00', 1, '2026-07-21T10:00:00')"));
        write.addBindValue(sourceRootId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));
        const auto assetId = write.lastInsertId().toLongLong();

        write.prepare(QStringLiteral(
            "UPDATE asset_file SET size_bytes = size_bytes + 1, modified_at = '2026-07-21T10:01:00' WHERE id = ?"));
        write.addBindValue(assetId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));
        write.prepare(QStringLiteral(
            "INSERT INTO media_metadata (asset_id, probe_status, updated_at) VALUES (?, 1, '2026-07-21T10:02:00')"));
        write.addBindValue(assetId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));
        write.prepare(QStringLiteral(
            "INSERT INTO embedded_metadata (asset_id, status, search_text, updated_at) "
            "VALUES (?, 1, 'camera metadata', '2026-07-21T10:03:00')"));
        write.addBindValue(assetId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));
        write.prepare(QStringLiteral(
            "INSERT INTO thumbnail (asset_id, status, image_path, updated_at) "
            "VALUES (?, 1, 'thumb.jpg', '2026-07-21T10:04:00')"));
        write.addBindValue(assetId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));
        write.prepare(QStringLiteral(
            "UPDATE folder_node SET name = 'CardRenamed', absolute_path = 'C:/CardRenamed', "
            "relative_path = 'CardRenamed' WHERE id = ?"));
        write.addBindValue(folderId);
        QVERIFY2(write.exec(), qPrintable(write.lastError().text()));

        QSqlQuery changes(db);
        QVERIFY2(changes.exec(QStringLiteral(
                     "SELECT entity_type, entity_id, operation, change_mask, entity_key, previous_entity_key "
                     "FROM catalog_change_log ORDER BY entity_type")),
                 qPrintable(changes.lastError().text()));
        QVERIFY(changes.next());
        QCOMPARE(changes.value(0).toInt(), 1);
        QCOMPARE(changes.value(1).toLongLong(), assetId);
        QCOMPARE(changes.value(2).toInt(), 1);
        QCOMPARE(changes.value(3).toInt(), 1 | 2 | 4);
        QVERIFY(changes.next());
        QCOMPARE(changes.value(0).toInt(), 2);
        QCOMPARE(changes.value(1).toLongLong(), folderId);
        QCOMPARE(changes.value(2).toInt(), 1);
        QCOMPARE(changes.value(3).toInt(), 8);
        QVERIFY(!changes.next());

        QSqlQuery monotonic(db);
        QVERIFY2(monotonic.exec(QStringLiteral(
                     "SELECT next_log_id, pending_change_count, requires_full_rebuild "
                     "FROM catalog_change_state WHERE singleton_id = 1")),
                 qPrintable(monotonic.lastError().text()));
        QVERIFY(monotonic.next());
        QVERIFY(monotonic.value(0).toLongLong() >= 7);
        QCOMPARE(monotonic.value(1).toInt(), 2);
        QCOMPARE(monotonic.value(2).toInt(), 1);

        QVERIFY2(write.exec(QStringLiteral("DROP TRIGGER catalog_change_thumbnail_update")),
                 qPrintable(write.lastError().text()));
        manager.closeProjectDatabase();
        QVERIFY2(manager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
        QSqlQuery repaired(manager.database());
        QVERIFY2(repaired.exec(QStringLiteral(
                     "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' "
                     "AND name = 'catalog_change_thumbnail_update'")),
                 qPrintable(repaired.lastError().text()));
        QVERIFY(repaired.next());
        QCOMPARE(repaired.value(0).toInt(), 1);
        manager.closeProjectDatabase();
    }

    void globalV7ToV10_createsBackupAndFolderSchema()
    {
        removeGlobalDatabaseFiles();
        QString errorMessage;
        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();
            QSqlQuery dropFolder(db);
            QVERIFY2(dropFolder.exec(QStringLiteral("DROP TABLE global_folder_node")),
                     qPrintable(dropFolder.lastError().text()));
            QSqlQuery downgrade(db);
            QVERIFY2(downgrade.exec(QStringLiteral("UPDATE schema_version SET version = 7")),
                     qPrintable(downgrade.lastError().text()));
            manager.closeDatabase();
        }

        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();

            QSqlQuery version(db);
            QVERIFY2(version.exec(QStringLiteral("SELECT version FROM schema_version")),
                     qPrintable(version.lastError().text()));
            QVERIFY(version.next());
            QCOMPARE(version.value(0).toInt(), GlobalDatabaseManager::CurrentSchemaVersion);

            QSqlQuery columns(db);
            QVERIFY2(columns.exec(QStringLiteral("PRAGMA table_info(global_folder_node)")),
                     qPrintable(columns.lastError().text()));
            QStringList names;
            while (columns.next()) {
                names.append(columns.value(1).toString());
            }
            QVERIFY(names.contains(QStringLiteral("folder_key")));
            QVERIFY(names.contains(QStringLiteral("parent_relative_path")));
            QVERIFY(names.contains(QStringLiteral("recursive_file_count")));
            QVERIFY(names.contains(QStringLiteral("normalized_date")));
            QVERIFY(names.contains(QStringLiteral("is_available")));
            QVERIFY(names.contains(QStringLiteral("sync_generation")));

            QSqlQuery assetColumns(db);
            QVERIFY2(assetColumns.exec(QStringLiteral("PRAGMA table_info(global_video_asset)")),
                     qPrintable(assetColumns.lastError().text()));
            QStringList assetColumnNames;
            while (assetColumns.next()) {
                assetColumnNames.append(assetColumns.value(1).toString());
            }
            QVERIFY(assetColumnNames.contains(QStringLiteral("capture_time")));
            QVERIFY(assetColumnNames.contains(QStringLiteral("capture_date")));
            QVERIFY(assetColumnNames.contains(QStringLiteral("capture_time_source")));
            QVERIFY(assetColumnNames.contains(QStringLiteral("capture_time_confidence")));
            QVERIFY(assetColumnNames.contains(QStringLiteral("embedded_metadata_text")));
            QVERIFY(assetColumnNames.contains(QStringLiteral("sync_generation")));

            QSqlQuery registryColumns(db);
            QVERIFY2(registryColumns.exec(QStringLiteral("PRAGMA table_info(project_registry)")),
                     qPrintable(registryColumns.lastError().text()));
            QStringList registryColumnNames;
            while (registryColumns.next()) {
                registryColumnNames.append(registryColumns.value(1).toString());
            }
            QVERIFY(registryColumnNames.contains(QStringLiteral("active_sync_generation")));

            const auto backupPath = DatabaseMigration::backupFilePath(
                globalDatabasePath(),
                GlobalDatabaseManager::CurrentSchemaVersion);
            QVERIFY(QFileInfo(backupPath).isFile());
            QCOMPARE(schemaVersionFromFile(backupPath, &errorMessage), 7);
            manager.closeDatabase();
        }
    }

    void globalV8ToV10_addsStructuredFactsPlanAndMarksLegacyRowsIncomplete()
    {
        removeGlobalDatabaseFiles();
        QString errorMessage;
        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();
            const QStringList downgradeStatements{
                QStringLiteral("DROP TABLE video_analysis_plan"),
                QStringLiteral("ALTER TABLE video_frame_analysis RENAME TO video_frame_analysis_v9"),
                QStringLiteral("CREATE TABLE video_frame_analysis ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, video_key TEXT NOT NULL, "
                               "frame_number INTEGER NOT NULL DEFAULT 0, timestamp_ms INTEGER NOT NULL DEFAULT 0, "
                               "image_path TEXT, caption TEXT, tags_json TEXT, objects_json TEXT, actions TEXT, setting_text TEXT, "
                               "error_message TEXT, analysis_state INTEGER NOT NULL DEFAULT 0, retry_count INTEGER NOT NULL DEFAULT 0, "
                               "last_http_status INTEGER NOT NULL DEFAULT 0, last_attempt_at TEXT NOT NULL DEFAULT '', "
                               "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE)"),
                QStringLiteral("DROP TABLE video_frame_analysis_v9"),
                QStringLiteral("INSERT INTO project_registry "
                               "(project_uuid, project_name, project_database_path, last_synced_at, sync_status, error_message) "
                               "VALUES ('visual-v8', 'Visual V8', 'visual-v8.cvdb', '', 'ok', '')"),
                QStringLiteral("INSERT INTO global_video_asset "
                               "(video_key, project_uuid, project_name, project_database_path, asset_id, file_name, absolute_path, relative_path) "
                               "VALUES ('legacy-frame', 'visual-v8', 'Visual V8', 'visual-v8.cvdb', 1, 'clip.mov', 'C:/clip.mov', 'clip.mov')"),
                QStringLiteral("INSERT INTO video_frame_analysis "
                               "(video_key, frame_number, timestamp_ms, image_path, caption, tags_json, objects_json, actions, setting_text, "
                               "error_message, analysis_state) "
                               "VALUES ('legacy-frame', 1, 0, 'frame.jpg', '旧版描述', '[]', '[]', '', '', '', 1)"),
                QStringLiteral("UPDATE schema_version SET version = 8")
            };
            QVERIFY2(executeStatements(db, downgradeStatements, &errorMessage), qPrintable(errorMessage));
            manager.closeDatabase();
        }

        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();

            QSqlQuery version(db);
            QVERIFY2(version.exec(QStringLiteral("SELECT version FROM schema_version")),
                     qPrintable(version.lastError().text()));
            QVERIFY(version.next());
            QCOMPARE(version.value(0).toInt(), GlobalDatabaseManager::CurrentSchemaVersion);

            QSqlQuery legacy(db);
            QVERIFY2(legacy.exec(QStringLiteral(
                         "SELECT entities_json, ocr_text, structured_profile_version, facts_complete, model_name, prompt_version "
                         "FROM video_frame_analysis WHERE video_key = 'legacy-frame'")),
                     qPrintable(legacy.lastError().text()));
            QVERIFY(legacy.next());
            QCOMPARE(legacy.value(0).toString(), QStringLiteral("[]"));
            QCOMPARE(legacy.value(1).toString(), QString());
            QCOMPARE(legacy.value(2).toInt(), 1);
            QCOMPARE(legacy.value(3).toInt(), 0);
            QCOMPARE(legacy.value(4).toString(), QString());
            QCOMPARE(legacy.value(5).toString(), QString());

            QSqlQuery planTable(db);
            QVERIFY2(planTable.exec(QStringLiteral(
                         "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'video_analysis_plan'")),
                     qPrintable(planTable.lastError().text()));
            QVERIFY(planTable.next());

            const auto backupPath = DatabaseMigration::backupFilePath(
                globalDatabasePath(),
                GlobalDatabaseManager::CurrentSchemaVersion);
            QVERIFY(QFileInfo(backupPath).isFile());
            QCOMPARE(schemaVersionFromFile(backupPath, &errorMessage), 8);
            manager.closeDatabase();
        }
    }

    void globalV9ToV10_createsBackupAndSemanticSearchSchema()
    {
        removeGlobalDatabaseFiles();
        QString errorMessage;
        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();
            const QStringList downgradeStatements{
                QStringLiteral("DROP INDEX IF EXISTS idx_search_document_key"),
                QStringLiteral("DROP INDEX IF EXISTS idx_search_document_type"),
                QStringLiteral("DROP INDEX IF EXISTS idx_search_index_state_singleton"),
                QStringLiteral("DROP TABLE search_index_state"),
                QStringLiteral("DROP TABLE search_document"),
                QStringLiteral("UPDATE schema_version SET version = 9")
            };
            QVERIFY2(executeStatements(db, downgradeStatements, &errorMessage), qPrintable(errorMessage));
            manager.closeDatabase();
        }

        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();

            QSqlQuery version(db);
            QVERIFY2(version.exec(QStringLiteral("SELECT version FROM schema_version")),
                     qPrintable(version.lastError().text()));
            QVERIFY(version.next());
            QCOMPARE(version.value(0).toInt(), GlobalDatabaseManager::CurrentSchemaVersion);

            QSqlQuery documentColumns(db);
            QVERIFY2(documentColumns.exec(QStringLiteral("PRAGMA table_info(search_document)")),
                     qPrintable(documentColumns.lastError().text()));
            QStringList documentColumnNames;
            while (documentColumns.next()) {
                documentColumnNames.append(documentColumns.value(1).toString());
            }
            const QStringList requiredDocumentColumns{
                QStringLiteral("id"),
                QStringLiteral("document_key"),
                QStringLiteral("document_type"),
                QStringLiteral("entity_key"),
                QStringLiteral("content_hash"),
                QStringLiteral("content_text"),
                QStringLiteral("source_updated_at"),
                QStringLiteral("model_id"),
                QStringLiteral("index_schema_version"),
                QStringLiteral("indexed_at")
            };
            for (const auto &column : requiredDocumentColumns) {
                QVERIFY2(documentColumnNames.contains(column), qPrintable(column));
            }

            QSqlQuery state(db);
            QVERIFY2(state.exec(QStringLiteral(
                         "SELECT singleton, schema_version, model_id, dimensions, generation, status, "
                         "document_count, updated_at, last_error FROM search_index_state")),
                     qPrintable(state.lastError().text()));
            QVERIFY(state.next());
            QCOMPARE(state.value(0).toInt(), 1);
            QCOMPARE(state.value(1).toInt(), 0);
            QCOMPARE(state.value(2).toString(), QString());
            QCOMPARE(state.value(3).toInt(), 0);
            QCOMPARE(state.value(4).toInt(), 0);
            QCOMPARE(state.value(5).toString(), QStringLiteral("dirty"));
            QCOMPARE(state.value(6).toInt(), 0);
            QVERIFY(!state.next());

            QSqlQuery indexes(db);
            QVERIFY2(indexes.exec(QStringLiteral(
                         "SELECT name FROM sqlite_master WHERE type = 'index' "
                         "AND name IN ('idx_search_document_key', 'idx_search_document_type', "
                         "'idx_search_index_state_singleton') ORDER BY name")),
                     qPrintable(indexes.lastError().text()));
            QStringList indexNames;
            while (indexes.next()) {
                indexNames.append(indexes.value(0).toString());
            }
            QCOMPARE(indexNames.size(), 3);

            const auto backupPath = DatabaseMigration::backupFilePath(
                globalDatabasePath(),
                GlobalDatabaseManager::CurrentSchemaVersion);
            QVERIFY(QFileInfo(backupPath).isFile());
            QVERIFY(QFileInfo(backupPath).size() > 0);
            QCOMPARE(schemaVersionFromFile(backupPath, &errorMessage), 9);
            manager.closeDatabase();
        }
    }

    void globalV10_repairsPartialSemanticSchemaAndInvalidatesIndex()
    {
        removeGlobalDatabaseFiles();
        QString errorMessage;
        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();
            const QStringList partialSchemaStatements{
                QStringLiteral("DROP INDEX IF EXISTS idx_search_document_key"),
                QStringLiteral("DROP INDEX IF EXISTS idx_search_document_type"),
                QStringLiteral("DROP INDEX IF EXISTS idx_search_index_state_singleton"),
                QStringLiteral("DROP TABLE search_document"),
                QStringLiteral("CREATE TABLE search_document ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, document_key TEXT, content_text TEXT)"),
                QStringLiteral("INSERT INTO search_document(document_key, content_text) VALUES "
                               "('duplicate', 'first'), ('duplicate', 'second'), ('', 'empty')"),
                QStringLiteral("DROP TABLE search_index_state"),
                QStringLiteral("CREATE TABLE search_index_state(singleton INTEGER, status TEXT)"),
                QStringLiteral("INSERT INTO search_index_state(singleton, status) VALUES "
                               "(1, 'ready'), (1, 'ready'), (2, 'ready')")
            };
            QVERIFY2(executeStatements(db, partialSchemaStatements, &errorMessage), qPrintable(errorMessage));
            manager.closeDatabase();
        }

        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();

            QSqlQuery documents(db);
            QVERIFY2(documents.exec(QStringLiteral(
                         "SELECT document_key, content_text, document_type, entity_key, content_hash, "
                         "source_updated_at, model_id, index_schema_version, indexed_at "
                         "FROM search_document ORDER BY id")),
                     qPrintable(documents.lastError().text()));
            QVERIFY(documents.next());
            QCOMPARE(documents.value(0).toString(), QStringLiteral("duplicate"));
            QCOMPARE(documents.value(1).toString(), QStringLiteral("first"));
            QCOMPARE(documents.value(2).toInt(), 0);
            QCOMPARE(documents.value(3).toString(), QString());
            QVERIFY(documents.next());
            QVERIFY(documents.value(0).toString().startsWith(QStringLiteral("legacy:")));
            QCOMPARE(documents.value(1).toString(), QStringLiteral("empty"));
            QVERIFY(!documents.next());

            QSqlQuery state(db);
            QVERIFY2(state.exec(QStringLiteral(
                         "SELECT singleton, schema_version, model_id, dimensions, generation, status, "
                         "document_count, updated_at, last_error FROM search_index_state")),
                     qPrintable(state.lastError().text()));
            QVERIFY(state.next());
            QCOMPARE(state.value(0).toInt(), 1);
            QCOMPARE(state.value(1).toInt(), 0);
            QCOMPARE(state.value(2).toString(), QString());
            QCOMPARE(state.value(3).toInt(), 0);
            QCOMPARE(state.value(4).toInt(), 0);
            QCOMPARE(state.value(5).toString(), QStringLiteral("dirty"));
            QCOMPARE(state.value(6).toInt(), 2);
            QCOMPARE(state.value(7).toString(), QString());
            QCOMPARE(state.value(8).toString(), QString());
            QVERIFY(!state.next());
            manager.closeDatabase();
        }
    }

    void globalV9ToV10_rollsBackAllSemanticSchemaChangesOnFailure()
    {
        removeGlobalDatabaseFiles();
        QString errorMessage;
        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();
            const QStringList downgradeStatements{
                QStringLiteral("DROP INDEX IF EXISTS idx_search_document_key"),
                QStringLiteral("DROP INDEX IF EXISTS idx_search_document_type"),
                QStringLiteral("DROP INDEX IF EXISTS idx_search_index_state_singleton"),
                QStringLiteral("DROP TABLE search_index_state"),
                QStringLiteral("DROP TABLE search_document"),
                QStringLiteral("CREATE TABLE idx_search_document_key(blocker INTEGER)"),
                QStringLiteral("UPDATE schema_version SET version = 9")
            };
            QVERIFY2(executeStatements(db, downgradeStatements, &errorMessage), qPrintable(errorMessage));
            manager.closeDatabase();
        }

        {
            GlobalDatabaseManager manager;
            QVERIFY(!manager.openDatabase(&errorMessage));
            QVERIFY(!errorMessage.trimmed().isEmpty());
        }

        QCOMPARE(schemaVersionFromFile(globalDatabasePath(), &errorMessage), 9);
        QCOMPARE(schemaVersionFromFile(DatabaseMigration::backupFilePath(
                                           globalDatabasePath(),
                                           GlobalDatabaseManager::CurrentSchemaVersion),
                                       &errorMessage),
                 9);

        const auto connectionName = QStringLiteral("verify_semantic_rollback");
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(globalDatabasePath());
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery tables(db);
        QVERIFY2(tables.exec(QStringLiteral(
                     "SELECT name FROM sqlite_master WHERE type = 'table' "
                     "AND name IN ('search_document', 'search_index_state', 'idx_search_document_key') "
                     "ORDER BY name")),
                 qPrintable(tables.lastError().text()));
        QStringList tableNames;
        while (tables.next()) {
            tableNames.append(tables.value(0).toString());
        }
        QCOMPARE(tableNames, QStringList{QStringLiteral("idx_search_document_key")});
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    void globalV8ToV10_rollsBackAllVisualSchemaChangesOnFailure()
    {
        removeGlobalDatabaseFiles();
        QString errorMessage;
        {
            GlobalDatabaseManager manager;
            QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
            auto db = manager.database();
            const QStringList downgradeStatements{
                QStringLiteral("DROP TABLE video_analysis_plan"),
                QStringLiteral("DROP INDEX idx_frame_visual_completeness"),
                QStringLiteral("ALTER TABLE video_frame_analysis RENAME TO video_frame_analysis_v9"),
                QStringLiteral("CREATE TABLE video_frame_analysis ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, video_key TEXT NOT NULL, "
                               "frame_number INTEGER NOT NULL DEFAULT 0, timestamp_ms INTEGER NOT NULL DEFAULT 0, "
                               "image_path TEXT, caption TEXT, tags_json TEXT, objects_json TEXT, actions TEXT, setting_text TEXT, "
                               "error_message TEXT, analysis_state INTEGER NOT NULL DEFAULT 0, retry_count INTEGER NOT NULL DEFAULT 0, "
                               "last_http_status INTEGER NOT NULL DEFAULT 0, last_attempt_at TEXT NOT NULL DEFAULT '')"),
                QStringLiteral("DROP TABLE video_frame_analysis_v9"),
                QStringLiteral("CREATE TABLE idx_frame_visual_completeness (blocker INTEGER)"),
                QStringLiteral("UPDATE schema_version SET version = 8")
            };
            QVERIFY2(executeStatements(db, downgradeStatements, &errorMessage), qPrintable(errorMessage));
            manager.closeDatabase();
        }

        {
            GlobalDatabaseManager manager;
            QVERIFY(!manager.openDatabase(&errorMessage));
            QVERIFY(!errorMessage.trimmed().isEmpty());
        }

        QCOMPARE(schemaVersionFromFile(globalDatabasePath(), &errorMessage), 8);
        QCOMPARE(schemaVersionFromFile(DatabaseMigration::backupFilePath(
                                           globalDatabasePath(),
                                           GlobalDatabaseManager::CurrentSchemaVersion),
                                       &errorMessage),
                 8);

        const auto connectionName = QStringLiteral("verify_visual_rollback");
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(globalDatabasePath());
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));

        QSqlQuery columns(db);
        QVERIFY2(columns.exec(QStringLiteral("PRAGMA table_info(video_frame_analysis)")),
                 qPrintable(columns.lastError().text()));
        QStringList columnNames;
        while (columns.next()) {
            columnNames.append(columns.value(1).toString());
        }
        QVERIFY(!columnNames.contains(QStringLiteral("entities_json")));
        QVERIFY(!columnNames.contains(QStringLiteral("facts_complete")));

        QSqlQuery planTable(db);
        QVERIFY2(planTable.exec(QStringLiteral(
                     "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'video_analysis_plan'")),
                 qPrintable(planTable.lastError().text()));
        QVERIFY(planTable.next());
        QCOMPARE(planTable.value(0).toInt(), 0);
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
};

QTEST_GUILESS_MAIN(FolderDatabaseMigrationTest)

#include "FolderDatabaseMigrationTest.moc"
