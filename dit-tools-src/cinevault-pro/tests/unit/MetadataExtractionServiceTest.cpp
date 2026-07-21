#include "application/MetadataExtractionService.h"
#include "core/jobs/JobEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/monitoring/PerformanceTelemetry.h"

#include <QtTest>

#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

class MetadataExtractionServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void extractsAndPersistsEmbeddedMetadataInBackground()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto sourcePath = tempDir.filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(sourcePath));

        DatabaseManager databaseManager;
        QString errorMessage;
        const auto databasePath = tempDir.filePath(QStringLiteral("project.cvdb"));
        QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
        auto db = databaseManager.database();

        QSqlQuery source(db);
        source.prepare(QStringLiteral(
            "INSERT INTO source_root (name, path, status, total_files, total_folders, total_size_bytes, "
            "video_count, audio_count, image_count, other_count, warning_count, scan_version, created_at, updated_at) "
            "VALUES ('Camera', ?, 'ok', 1, 0, 0, 0, 0, 1, 0, 0, 1, ?, ?)"));
        source.addBindValue(sourcePath);
        source.addBindValue(QStringLiteral("2026-07-15T12:00:00"));
        source.addBindValue(QStringLiteral("2026-07-15T12:00:00"));
        QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
        const auto sourceRootId = source.lastInsertId().toLongLong();

        ExifToolAdapter adapter;
        QVERIFY2(adapter.isAvailable(), qPrintable(adapter.unavailableReason()));
        const auto imagePath = QDir(sourcePath).filePath(QStringLiteral("camera.jpg"));
        QImage image(12, 10, QImage::Format_RGB32);
        image.fill(QColor(QStringLiteral("#22a06b")));
        QVERIFY(image.save(imagePath, "JPEG"));

        QProcess writer;
        writer.setProgram(adapter.executablePath());
        writer.setArguments({
            QStringLiteral("-overwrite_original"),
            QStringLiteral("-EXIF:Make=End To End Make"),
            QStringLiteral("-EXIF:Model=End To End Model"),
            QStringLiteral("-EXIF:DateTimeOriginal=2026:07:15 18:20:30"),
            imagePath
        });
        writer.start();
        QVERIFY(writer.waitForStarted(5000));
        QVERIFY(writer.waitForFinished(30000));
        QCOMPARE(writer.exitCode(), 0);

        const QFileInfo imageInfo(imagePath);
        QSqlQuery asset(db);
        asset.prepare(QStringLiteral(
            "INSERT INTO asset_file (source_root_id, name, extension, absolute_path, relative_path, parent_path, "
            "path_key, asset_type, size_bytes, modified_at, is_readable, created_at) "
            "VALUES (?, ?, 'jpg', ?, ?, ?, ?, ?, ?, ?, 1, ?)"));
        asset.addBindValue(sourceRootId);
        asset.addBindValue(imageInfo.fileName());
        asset.addBindValue(imageInfo.absoluteFilePath());
        asset.addBindValue(imageInfo.fileName());
        asset.addBindValue(imageInfo.absolutePath());
        asset.addBindValue(imageInfo.absoluteFilePath().toCaseFolded());
        asset.addBindValue(static_cast<int>(AssetType::Image));
        asset.addBindValue(imageInfo.size());
        asset.addBindValue(imageInfo.lastModified().toString(Qt::ISODateWithMs));
        asset.addBindValue(QStringLiteral("2026-07-15T18:20:30"));
        QVERIFY2(asset.exec(), qPrintable(asset.lastError().text()));
        const auto assetId = asset.lastInsertId().toLongLong();

        JobEngine jobEngine(&databaseManager);
        MetadataExtractionService service(&databaseManager, &jobEngine, &adapter);
        QSignalSpy changedSpy(&service, &MetadataExtractionService::metadataCatalogChanged);
        service.startForSourceRoot(sourceRootId);
        QVERIFY2(changedSpy.wait(30000), "后台真实元数据任务未在超时前完成");

        QSqlQuery metadata(db);
        metadata.prepare(QStringLiteral(
            "SELECT status, camera_make, camera_model, capture_time, width, height, search_text, error_message "
            "FROM embedded_metadata WHERE asset_id = ?"));
        metadata.addBindValue(assetId);
        QVERIFY2(metadata.exec(), qPrintable(metadata.lastError().text()));
        QVERIFY(metadata.next());
        QCOMPARE(metadata.value(0).toInt(), static_cast<int>(ProbeStatus::Success));
        QCOMPARE(metadata.value(1).toString(), QStringLiteral("End To End Make"));
        QCOMPARE(metadata.value(2).toString(), QStringLiteral("End To End Model"));
        QVERIFY(metadata.value(3).toString().startsWith(QStringLiteral("2026-07-15T18:20:30")));
        QCOMPARE(metadata.value(4).toInt(), 12);
        QCOMPARE(metadata.value(5).toInt(), 10);
        QVERIFY(metadata.value(6).toString().contains(QStringLiteral("End To End Make")));
        QVERIFY(metadata.value(7).toString().isEmpty());

        QTRY_VERIFY_WITH_TIMEOUT(!jobEngine.jobs().isEmpty()
                                     && jobEngine.jobs().first().state == JobState::Completed,
                                 5000);
        databaseManager.closeProjectDatabase();
    }

    void extractionPaginationKeepsAssetQueueBounded()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto sourcePath = tempDir.filePath(QStringLiteral("paged-source"));
        QVERIFY(QDir().mkpath(sourcePath));

        DatabaseManager databaseManager;
        QString errorMessage;
        const auto databasePath = tempDir.filePath(QStringLiteral("paged-project.cvdb"));
        QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
        auto db = databaseManager.database();

        QSqlQuery source(db);
        source.prepare(QStringLiteral(
            "INSERT INTO source_root (name, path, status, total_files, total_folders, total_size_bytes, "
            "video_count, audio_count, image_count, other_count, warning_count, scan_version, created_at, updated_at) "
            "VALUES ('PagedCamera', ?, 'ok', 129, 0, 0, 0, 0, 129, 0, 0, 5, ?, ?)"));
        source.addBindValue(sourcePath);
        source.addBindValue(QStringLiteral("2026-07-21T12:00:00"));
        source.addBindValue(QStringLiteral("2026-07-21T12:00:00"));
        QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
        const auto sourceRootId = source.lastInsertId().toLongLong();

        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(QColor(QStringLiteral("#3b82f6")));
        QSqlQuery asset(db);
        asset.prepare(QStringLiteral(
            "INSERT INTO asset_file (source_root_id, name, extension, absolute_path, relative_path, parent_path, "
            "path_key, asset_type, size_bytes, modified_at, is_readable, created_at) "
            "VALUES (?, ?, 'jpg', ?, ?, ?, ?, ?, ?, ?, 1, ?)"));
        for (int index = 0; index < 129; ++index) {
            const auto fileName = QStringLiteral("paged-%1.jpg").arg(index, 3, 10, QLatin1Char('0'));
            const auto imagePath = QDir(sourcePath).filePath(fileName);
            QVERIFY(image.save(imagePath, "JPEG"));
            const QFileInfo imageInfo(imagePath);
            asset.addBindValue(sourceRootId);
            asset.addBindValue(fileName);
            asset.addBindValue(imageInfo.absoluteFilePath());
            asset.addBindValue(fileName);
            asset.addBindValue(imageInfo.absolutePath());
            asset.addBindValue(imageInfo.absoluteFilePath().toCaseFolded());
            asset.addBindValue(static_cast<int>(AssetType::Image));
            asset.addBindValue(imageInfo.size());
            asset.addBindValue(imageInfo.lastModified().toString(Qt::ISODateWithMs));
            asset.addBindValue(QStringLiteral("2026-07-21T12:00:00"));
            QVERIFY2(asset.exec(), qPrintable(asset.lastError().text()));
            asset.finish();
        }

        ExifToolAdapter adapter;
        QVERIFY2(adapter.isAvailable(), qPrintable(adapter.unavailableReason()));
        auto &telemetry = PerformanceTelemetry::global();
        telemetry.resetForTesting();
        JobEngine jobEngine(&databaseManager);
        MetadataExtractionService service(&databaseManager, &jobEngine, &adapter);
        QSignalSpy changedSpy(&service, &MetadataExtractionService::metadataCatalogChanged);
        service.startForSourceRoot(sourceRootId);
        QVERIFY2(changedSpy.wait(60000), "分页元数据任务未在超时前完成");

        QSqlQuery metadataCount(db);
        QVERIFY(metadataCount.exec(QStringLiteral("SELECT COUNT(*) FROM embedded_metadata"))
                && metadataCount.next());
        QCOMPARE(metadataCount.value(0).toLongLong(), qint64{129});
        metadataCount.finish();
        const auto peakDepth = telemetry.snapshot()
            .value(QStringLiteral("peak_queue_depths")).toObject()
            .value(QStringLiteral("metadata.exif_assets")).toInteger();
        QCOMPARE(peakDepth, qint64{128});
        databaseManager.closeProjectDatabase();
    }
};

QTEST_GUILESS_MAIN(MetadataExtractionServiceTest)

#include "MetadataExtractionServiceTest.moc"
