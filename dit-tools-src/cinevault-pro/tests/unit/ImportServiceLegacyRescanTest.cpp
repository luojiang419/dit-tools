#include "application/ImportService.h"
#include "application/JobService.h"
#include "core/jobs/JobEngine.h"
#include "core/scan/FileTypeService.h"
#include "core/scan/ScanEngine.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {
void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(content), content.size());
}

qint64 countAssets(QSqlDatabase db, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM asset_file"));
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }
    return query.value(0).toLongLong();
}

QStringList assetNames(QSqlDatabase db)
{
    QStringList names;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT name FROM asset_file ORDER BY name"))) {
        return names;
    }
    while (query.next()) {
        names.append(query.value(0).toString());
    }
    return names;
}

int scanVersion(QSqlDatabase db, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT scan_version FROM source_root LIMIT 1"));
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }
    return query.value(0).toInt();
}

qint64 scanSessionCount(QSqlDatabase db, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM scan_session")) || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }
    return query.value(0).toLongLong();
}

qint64 completedScanWorkItemCount(QSqlDatabase db, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM scan_work_item WHERE state = 'completed'")) || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }
    return query.value(0).toLongLong();
}

QVariantList folderRow(QSqlDatabase db, const QString &relativePath, QString *errorMessage)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT parent_relative_path, depth, direct_file_count, recursive_file_count, normalized_date, date_anchor, path_key "
        "FROM folder_node WHERE relative_path = ?"));
    query.addBindValue(relativePath.isNull() ? QStringLiteral("") : relativePath);
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return {};
    }
    QVariantList row;
    for (int index = 0; index < 7; ++index) {
        row.append(query.value(index));
    }
    return row;
}

qint64 insertSourceRoot(QSqlDatabase db, const QString &name, const QString &path)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, created_at, updated_at) VALUES (?, ?, 'ok', ?, ?)"));
    query.addBindValue(name);
    query.addBindValue(QFileInfo(path).absoluteFilePath());
    query.addBindValue(QStringLiteral("2026-07-15T10:00:00"));
    query.addBindValue(QStringLiteral("2026-07-15T10:00:00"));
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

SourceRoot sourceRootById(QSqlDatabase db, qint64 sourceRootId)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, "
        "video_count, audio_count, image_count, other_count, warning_count, scan_version "
        "FROM source_root WHERE id = ?"));
    query.addBindValue(sourceRootId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    SourceRoot source;
    source.id = query.value(0).toLongLong();
    source.name = query.value(1).toString();
    source.path = query.value(2).toString();
    source.status = query.value(3).toString();
    source.totalFiles = query.value(4).toLongLong();
    source.totalFolders = query.value(5).toLongLong();
    source.totalSizeBytes = query.value(6).toLongLong();
    source.videoCount = query.value(7).toLongLong();
    source.audioCount = query.value(8).toLongLong();
    source.imageCount = query.value(9).toLongLong();
    source.otherCount = query.value(10).toLongLong();
    source.warningCount = query.value(11).toLongLong();
    source.scanVersion = query.value(12).toInt();
    return source;
}
}

class ImportServiceLegacyRescanTest : public QObject {
    Q_OBJECT

private slots:
    void atomicRescan_failureKeepsPreviousCatalogAndSuccessfulRetryPreservesIdentity()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto projectDb = QDir(temp.path()).filePath(QStringLiteral("atomic.cvdb"));
        const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("FullDisk"));
        const auto firstPath = QDir(sourcePath).filePath(QStringLiteral("Camera/A001.mov"));
        writeFile(firstPath, "first-version");
        writeFile(QDir(sourcePath).filePath(QStringLiteral("Notes/shot.txt")), "notes");

        DatabaseManager databaseManager;
        QString errorMessage;
        QVERIFY2(databaseManager.openProjectDatabase(projectDb, &errorMessage), qPrintable(errorMessage));
        const auto sourceRootId = insertSourceRoot(databaseManager.database(), QStringLiteral("FullDisk"), sourcePath);
        QVERIFY(sourceRootId > 0);

        JobEngine jobEngine(&databaseManager);
        ScanEngine scanEngine(&databaseManager, &jobEngine, nullptr, nullptr);
        QSignalSpy scanFinished(&scanEngine, &ScanEngine::scanFinished);
        QSignalSpy scanFailed(&scanEngine, &ScanEngine::scanFailed);

        const auto firstJobId = jobEngine.createJob(JobType::Scan,
                                                    QStringLiteral("首次扫描"),
                                                    QStringLiteral("建立原子扫描基线"),
                                                    sourceRootId);
        scanEngine.startScan(sourceRootById(databaseManager.database(), sourceRootId), firstJobId);
        QVERIFY(scanFinished.wait(10000));
        scanEngine.waitForIdle();
        QCOMPARE(countAssets(databaseManager.database(), &errorMessage), qint64{2});

        QSqlQuery readIdentity(databaseManager.database());
        readIdentity.prepare(QStringLiteral("SELECT id FROM asset_file WHERE name = 'A001.mov'"));
        QVERIFY2(readIdentity.exec() && readIdentity.next(), qPrintable(readIdentity.lastError().text()));
        const auto originalAssetId = readIdentity.value(0).toLongLong();
        QVERIFY(originalAssetId > 0);
        readIdentity.finish();

        QSqlQuery favorite(databaseManager.database());
        favorite.prepare(QStringLiteral("UPDATE asset_file SET is_favorite = 1 WHERE id = ?"));
        favorite.addBindValue(originalAssetId);
        QVERIFY2(favorite.exec(), qPrintable(favorite.lastError().text()));

        QSqlQuery metadata(databaseManager.database());
        metadata.prepare(QStringLiteral(
            "INSERT INTO media_metadata "
            "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, raw_json, error_message, updated_at) "
            "VALUES (?, 1, 1, 'mov', 1000, 100, '{}', '', '2026-07-15T10:00:00')"));
        metadata.addBindValue(originalAssetId);
        QVERIFY2(metadata.exec(), qPrintable(metadata.lastError().text()));

        writeFile(QDir(sourcePath).filePath(QStringLiteral("Added/new.jpg")), "new-image");
        scanEngine.setFailureAfterEntriesForTesting(1);
        scanFinished.clear();
        const auto failedJobId = jobEngine.createJob(JobType::Scan,
                                                     QStringLiteral("故障扫描"),
                                                     QStringLiteral("验证旧目录保护"),
                                                     sourceRootId);
        scanEngine.startScan(sourceRootById(databaseManager.database(), sourceRootId), failedJobId);
        QVERIFY(scanFailed.wait(10000));
        scanEngine.waitForIdle();

        QCOMPARE(countAssets(databaseManager.database(), &errorMessage), qint64{2});
        QSqlQuery preserved(databaseManager.database());
        preserved.prepare(QStringLiteral(
            "SELECT af.id, af.is_favorite, COUNT(mm.asset_id) "
            "FROM asset_file af LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
            "WHERE af.name = 'A001.mov' GROUP BY af.id, af.is_favorite"));
        QVERIFY2(preserved.exec() && preserved.next(), qPrintable(preserved.lastError().text()));
        QCOMPARE(preserved.value(0).toLongLong(), originalAssetId);
        QCOMPARE(preserved.value(1).toInt(), 1);
        QCOMPARE(preserved.value(2).toInt(), 1);
        preserved.finish();

        scanEngine.setFailureAfterEntriesForTesting(-1);
        scanFailed.clear();
        scanFinished.clear();
        const auto retryJobId = jobEngine.createJob(JobType::Scan,
                                                    QStringLiteral("重试扫描"),
                                                    QStringLiteral("验证原子切换"),
                                                    sourceRootId);
        scanEngine.startScan(sourceRootById(databaseManager.database(), sourceRootId), retryJobId);
        QVERIFY(scanFinished.wait(10000));
        scanEngine.waitForIdle();

        const auto retryNames = assetNames(databaseManager.database());
        QCOMPARE(retryNames.join(QLatin1Char(',')),
                 QStringLiteral("A001.mov,new.jpg,shot.txt"));
        QSqlQuery afterRetry(databaseManager.database());
        afterRetry.prepare(QStringLiteral("SELECT id, is_favorite FROM asset_file WHERE name = 'A001.mov'"));
        QVERIFY2(afterRetry.exec() && afterRetry.next(), qPrintable(afterRetry.lastError().text()));
        QCOMPARE(afterRetry.value(0).toLongLong(), originalAssetId);
        QCOMPARE(afterRetry.value(1).toInt(), 1);
    }

    void rescanLegacySourceRoots_rebuildsOldVideoOnlyIndex()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto projectDb = QDir(temp.path()).filePath(QStringLiteral("project.cvdb"));
        const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("CardA"));
        writeFile(QDir(sourcePath).filePath(QStringLiteral("A001.mov")), "video");
        writeFile(QDir(sourcePath).filePath(QStringLiteral("Audio/A001.wav")), "audio");
        writeFile(QDir(sourcePath).filePath(QStringLiteral("Notes/shot.md")), "notes");
        writeFile(QDir(sourcePath).filePath(QStringLiteral("2026-07-14/CameraA/B001.mov")), "video2");

        DatabaseManager databaseManager;
        QString errorMessage;
        QVERIFY2(databaseManager.openProjectDatabase(projectDb, &errorMessage), qPrintable(errorMessage));

        QSqlQuery sourceInsert(databaseManager.database());
        sourceInsert.prepare(QStringLiteral(
            "INSERT INTO source_root "
            "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, scan_version, created_at, updated_at) "
            "VALUES ('CardA', ?, 'ok', 1, 0, 5, 1, 0, 0, 0, 0, 0, '2026-07-04T10:00:00', '2026-07-04T10:00:00')"));
        sourceInsert.addBindValue(QFileInfo(sourcePath).absoluteFilePath());
        QVERIFY2(sourceInsert.exec(), qPrintable(sourceInsert.lastError().text()));
        const auto sourceRootId = sourceInsert.lastInsertId().toLongLong();

        QSqlQuery oldAsset(databaseManager.database());
        oldAsset.prepare(QStringLiteral(
            "INSERT INTO asset_file "
            "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, size_bytes, modified_at, is_readable, created_at) "
            "VALUES (?, 'A001.mov', 'mov', ?, 'A001.mov', ?, ?, 5, '2026-07-04T10:00:00', 1, '2026-07-04T10:00:00')"));
        oldAsset.addBindValue(sourceRootId);
        oldAsset.addBindValue(QDir(sourcePath).filePath(QStringLiteral("A001.mov")));
        oldAsset.addBindValue(QFileInfo(sourcePath).absoluteFilePath());
        oldAsset.addBindValue(static_cast<int>(AssetType::Video));
        QVERIFY2(oldAsset.exec(), qPrintable(oldAsset.lastError().text()));
        QCOMPARE(countAssets(databaseManager.database(), &errorMessage), qint64{1});

        JobEngine jobEngine(&databaseManager);
        JobService jobService(&jobEngine);
        ScanEngine scanEngine(&databaseManager, &jobEngine, nullptr, nullptr);
        ImportService importService(&databaseManager, &jobService, &scanEngine);
        QSignalSpy scanFinished(&scanEngine, &ScanEngine::scanFinished);

        importService.rescanLegacySourceRoots();

        QVERIFY(scanFinished.wait(10000));
        scanEngine.waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(countAssets(databaseManager.database(), &errorMessage), qint64{4});
        QCOMPARE(scanVersion(databaseManager.database(), &errorMessage), ScanEngine::CurrentScanVersion);

        const auto root = folderRow(databaseManager.database(), QString(), &errorMessage);
        QCOMPARE(root.size(), 7);
        QCOMPARE(root.at(1).toInt(), 0);
        QCOMPARE(root.at(2).toLongLong(), qint64{1});
        QCOMPARE(root.at(3).toLongLong(), qint64{4});
        QVERIFY(!root.at(6).toString().isEmpty());

        const auto dateFolder = folderRow(databaseManager.database(), QStringLiteral("2026-07-14"), &errorMessage);
        QCOMPARE(dateFolder.size(), 7);
        QCOMPARE(dateFolder.at(0).toString(), QString());
        QCOMPARE(dateFolder.at(1).toInt(), 1);
        QCOMPARE(dateFolder.at(2).toLongLong(), qint64{0});
        QCOMPARE(dateFolder.at(3).toLongLong(), qint64{1});
        QCOMPARE(dateFolder.at(4).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(dateFolder.at(5).toString(), QStringLiteral("2026-07-14"));

        const auto cameraFolder = folderRow(databaseManager.database(), QStringLiteral("2026-07-14/CameraA"), &errorMessage);
        QCOMPARE(cameraFolder.size(), 7);
        QCOMPARE(cameraFolder.at(0).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(cameraFolder.at(1).toInt(), 2);
        QCOMPARE(cameraFolder.at(2).toLongLong(), qint64{1});
        QCOMPARE(cameraFolder.at(3).toLongLong(), qint64{1});
        QCOMPARE(cameraFolder.at(4).toString(), QStringLiteral("2026-07-14"));
        QCOMPARE(cameraFolder.at(5).toString(), QStringLiteral("2026-07-14"));
    }

    void interruptedScan_resumesFromPersistedDirectoryCheckpointAfterProjectReopen()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto projectDb = QDir(temp.path()).filePath(QStringLiteral("resume.cvdb"));
        const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("CardB"));
        writeFile(QDir(sourcePath).filePath(QStringLiteral("A/clip-a.mov")), "a");
        writeFile(QDir(sourcePath).filePath(QStringLiteral("B/clip-b.mov")), "b");
        writeFile(QDir(sourcePath).filePath(QStringLiteral("B/notes.txt")), "notes");

        DatabaseManager databaseManager;
        QString errorMessage;
        QVERIFY2(databaseManager.openProjectDatabase(projectDb, &errorMessage), qPrintable(errorMessage));
        const auto sourceRootId = insertSourceRoot(databaseManager.database(), QStringLiteral("CardB"), sourcePath);
        QVERIFY(sourceRootId > 0);

        {
            JobEngine initialJobs(&databaseManager);
            ScanEngine initialScan(&databaseManager, &initialJobs, nullptr, nullptr);
            // 根目录先提交 A/B 两个子目录检查点，随后在 A 目录中断，
            // 以验证重启后不会从已完成的根目录检查点重新开始。
            initialScan.setFailureAfterEntriesForTesting(3);
            const auto jobId = initialJobs.createJob(JobType::Scan,
                                                     QStringLiteral("中断扫描"),
                                                     QStringLiteral("模拟异常退出前的检查点"),
                                                     sourceRootId);
            initialScan.startScan(sourceRootById(databaseManager.database(), sourceRootId), jobId);
            initialScan.waitForIdle();
            QCoreApplication::processEvents();
        }

        QCOMPARE(countAssets(databaseManager.database(), &errorMessage), qint64{0});
        QCOMPARE(scanSessionCount(databaseManager.database(), &errorMessage), qint64{1});
        QCOMPARE(completedScanWorkItemCount(databaseManager.database(), &errorMessage), qint64{1});

        databaseManager.closeProjectDatabase();
        QVERIFY2(databaseManager.openProjectDatabase(projectDb, &errorMessage), qPrintable(errorMessage));

        JobEngine resumedJobs(&databaseManager);
        JobService jobService(&resumedJobs);
        ScanEngine resumedScan(&databaseManager, &resumedJobs, nullptr, nullptr);
        ImportService importService(&databaseManager, &jobService, &resumedScan);

        importService.resumeInterruptedScans();

        resumedScan.waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(assetNames(databaseManager.database()).join(QLatin1Char(',')),
                 QStringLiteral("clip-a.mov,clip-b.mov,notes.txt"));
        QCOMPARE(scanSessionCount(databaseManager.database(), &errorMessage), qint64{0});
    }
};

QTEST_GUILESS_MAIN(ImportServiceLegacyRescanTest)

#include "ImportServiceLegacyRescanTest.moc"
