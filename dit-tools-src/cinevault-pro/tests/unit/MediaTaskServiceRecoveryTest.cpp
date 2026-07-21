#include "application/MediaTaskService.h"
#include "core/jobs/JobEngine.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/monitoring/PerformanceTelemetry.h"
#include "shared/Paths.h"
#include "shared/ThumbnailCacheQuota.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QSemaphore>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>
#include <stdexcept>

namespace {
class FakeThumbnailEngine : public ThumbnailEngine {
public:
    FakeThumbnailEngine()
        : ThumbnailEngine(nullptr, nullptr)
    {
    }

    ThumbnailResult createPlaceholder(const ThumbnailRequest &request) const override
    {
        ThumbnailResult result;
        result.assetId = request.assetId;
        result.outputPath = request.cachePath;

        QDir().mkpath(QFileInfo(request.cachePath).absolutePath());
        QFile file(request.cachePath);
        result.success = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if (result.success) {
            file.write("fake-thumbnail");
            file.close();
        } else {
            result.errorMessage = file.errorString();
        }
        return result;
    }
};

class BlockingThumbnailEngine : public FakeThumbnailEngine {
public:
    ThumbnailResult createPlaceholder(const ThumbnailRequest &request) const override
    {
        m_started.release();
        m_continue.acquire();
        return FakeThumbnailEngine::createPlaceholder(request);
    }

    bool waitUntilStarted(int timeoutMs) const
    {
        return m_started.tryAcquire(1, timeoutMs);
    }

    void resume() const
    {
        m_continue.release();
    }

private:
    mutable QSemaphore m_started;
    mutable QSemaphore m_continue;
};

class ThrowingThumbnailEngine : public FakeThumbnailEngine {
public:
    ThumbnailResult createPlaceholder(const ThumbnailRequest &request) const override
    {
        if (QFileInfo(request.sourcePath).fileName() == QStringLiteral("tool-crash.mp4")) {
            throw std::runtime_error("simulated external tool crash");
        }
        return FakeThumbnailEngine::createPlaceholder(request);
    }
};

bool execSql(QSqlDatabase db, const QString &sql, QString *errorMessage = nullptr)
{
    QSqlQuery query(db);
    if (query.exec(sql)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

qint64 insertAsset(QSqlDatabase db, qint64 sourceRootId, const QString &sourcePath, const QString &fileName)
{
    const auto filePath = QDir(sourcePath).filePath(fileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return 0;
    }
    file.write("video");
    file.close();

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO asset_file "
        "(source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, size_bytes, modified_at, "
        "is_readable, created_at) "
        "VALUES (?, ?, 'mp4', ?, ?, ?, ?, 12, '2026-07-06T12:00:00', 1, '2026-07-06T12:00:00')"));
    query.addBindValue(sourceRootId);
    query.addBindValue(fileName);
    query.addBindValue(filePath);
    query.addBindValue(fileName);
    query.addBindValue(sourcePath);
    query.addBindValue(static_cast<int>(AssetType::Video));
    return query.exec() ? query.lastInsertId().toLongLong() : 0;
}

void insertThumbnail(QSqlDatabase db, qint64 assetId, ThumbnailStatus status, const QString &imagePath)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO thumbnail (asset_id, status, image_path, updated_at, error_message) "
        "VALUES (?, ?, ?, '2026-07-06T12:00:00', '')"));
    query.addBindValue(assetId);
    query.addBindValue(static_cast<int>(status));
    query.addBindValue(imagePath);
    QVERIFY(query.exec());
}

void insertCompletedMetadata(QSqlDatabase db, qint64 assetId)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO media_metadata "
        "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, raw_json, error_message, updated_at) "
        "VALUES (?, ?, ?, 'mp4', 0, 0, '{}', '', '2026-07-21T12:00:00')"));
    query.addBindValue(assetId);
    query.addBindValue(static_cast<int>(ProbeStatus::Success));
    query.addBindValue(static_cast<int>(AssetType::Video));
    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
}

QPair<ThumbnailStatus, QString> readThumbnail(QSqlDatabase db, qint64 assetId)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT status, COALESCE(image_path, '') FROM thumbnail WHERE asset_id = ?"));
    query.addBindValue(assetId);
    if (!query.exec() || !query.next()) {
        return {ThumbnailStatus::Pending, QString()};
    }
    return {static_cast<ThumbnailStatus>(query.value(0).toInt()), query.value(1).toString()};
}
}

class MediaTaskServiceRecoveryTest : public QObject {
    Q_OBJECT

private slots:
    void recoversRunningEmptyThumbnails();
    void unchangedCompletedAssetsDoNotCreateDuplicateJobs();
    void simultaneousRecoveryTriggersCreateOnlyOneWorker();
    void newImportsGenerateThumbnailBeforeMetadataProbe();
    void thumbnailPaginationThrottlesHighFrequencyUpdates();
    void singleThumbnailDatabaseFailureDoesNotAbortRemainingSource();
    void recoveryMarkerAuditsMissingCacheReferencesBeforeDispatch();
};

void MediaTaskServiceRecoveryTest::recoversRunningEmptyThumbnails()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    QVERIFY2(execSql(db,
                    QStringLiteral("INSERT INTO project (id, name, root_path, created_at) "
                                   "VALUES ('project-1', 'Project', '%1', '2026-07-06T12:00:00')")
                        .arg(tempDir.path().replace("'", "''")),
                    &errorMessage),
             qPrintable(errorMessage));

    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, "
        "warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 3, 0, 36, 3, 0, 0, 0, 0, 2, '2026-07-06T12:00:00', '2026-07-06T12:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY(source.exec());
    const auto sourceRootId = source.lastInsertId().toLongLong();
    QVERIFY(sourceRootId > 0);

    const auto staleAssetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("stale.mp4"));
    const auto missingAssetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("missing.mp4"));
    const auto finishedAssetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("finished.mp4"));
    QVERIFY(staleAssetId > 0);
    QVERIFY(missingAssetId > 0);
    QVERIFY(finishedAssetId > 0);

    const auto finishedPath = QDir(tempDir.path()).filePath(QStringLiteral("finished.jpg"));
    insertThumbnail(db, staleAssetId, ThumbnailStatus::Running, QString());
    insertThumbnail(db, finishedAssetId, ThumbnailStatus::Success, finishedPath);

    JobEngine jobEngine(&databaseManager);
    FakeThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    service.recoverStaleThumbnails();

    QTRY_VERIFY_WITH_TIMEOUT(readThumbnail(db, staleAssetId).first == ThumbnailStatus::Success, 10000);
    const auto staleRow = readThumbnail(db, staleAssetId);
    QVERIFY(!staleRow.second.isEmpty());
    QVERIFY2(QFileInfo::exists(staleRow.second), qPrintable(staleRow.second));

    QTRY_VERIFY_WITH_TIMEOUT(readThumbnail(db, missingAssetId).first == ThumbnailStatus::Success, 10000);
    const auto missingRow = readThumbnail(db, missingAssetId);
    QVERIFY(!missingRow.second.isEmpty());
    QVERIFY2(QFileInfo::exists(missingRow.second), qPrintable(missingRow.second));

    const auto finishedRow = readThumbnail(db, finishedAssetId);
    QCOMPARE(finishedRow.first, ThumbnailStatus::Success);
    QCOMPARE(finishedRow.second, finishedPath);

    QTRY_VERIFY_WITH_TIMEOUT(([&jobEngine]() {
        const auto jobs = jobEngine.jobs();
        return std::any_of(jobs.cbegin(), jobs.cend(), [](const Job &job) {
            return job.type == JobType::Thumbnail && job.state == JobState::Completed;
        });
    })(), 10000);
    databaseManager.closeProjectDatabase();
}

void MediaTaskServiceRecoveryTest::unchangedCompletedAssetsDoNotCreateDuplicateJobs()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 1, 0, 12, 1, 0, 0, 0, 0, 2, '2026-07-06T12:00:00', '2026-07-06T12:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
    const auto sourceRootId = source.lastInsertId().toLongLong();
    const auto assetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("complete.mp4"));
    QVERIFY(assetId > 0);

    QSqlQuery metadata(db);
    metadata.prepare(QStringLiteral(
        "INSERT INTO media_metadata "
        "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, raw_json, error_message, updated_at) "
        "VALUES (?, ?, ?, 'mp4', 1000, 1000000, '{}', '', '2026-07-06T12:00:00')"));
    metadata.addBindValue(assetId);
    metadata.addBindValue(static_cast<int>(ProbeStatus::Success));
    metadata.addBindValue(static_cast<int>(AssetType::Video));
    QVERIFY2(metadata.exec(), qPrintable(metadata.lastError().text()));
    insertThumbnail(db, assetId, ThumbnailStatus::Success, tempDir.filePath(QStringLiteral("complete.jpg")));

    JobEngine jobEngine(&databaseManager);
    FakeThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    service.startForSourceRoot(sourceRootId);
    QCoreApplication::processEvents();

    QVERIFY2(jobEngine.jobs().isEmpty(), "未变化且已完成的素材不应重复创建媒体解析任务");
    databaseManager.closeProjectDatabase();
}

void MediaTaskServiceRecoveryTest::simultaneousRecoveryTriggersCreateOnlyOneWorker()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 1, 0, 12, 1, 0, 0, 0, 0, 4, '2026-07-17T08:00:00', '2026-07-17T08:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
    const auto sourceRootId = source.lastInsertId().toLongLong();
    QVERIFY(insertAsset(db, sourceRootId, sourcePath, QStringLiteral("duplicate.mp4")) > 0);

    JobEngine jobEngine(&databaseManager);
    BlockingThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    service.startForSourceRoot(sourceRootId);

    const bool workerStarted = thumbnailEngine.waitUntilStarted(10000);
    if (!workerStarted) {
        thumbnailEngine.resume();
    }
    QVERIFY2(workerStarted, "首个媒体 worker 未启动");
    const auto originalJobCount = jobEngine.jobs().size();
    QVERIFY(originalJobCount > 0);

    service.startForSourceRoot(sourceRootId);
    QCOMPARE(jobEngine.jobs().size(), originalJobCount);

    thumbnailEngine.resume();
    service.waitForIdle();
    databaseManager.closeProjectDatabase();
}

void MediaTaskServiceRecoveryTest::newImportsGenerateThumbnailBeforeMetadataProbe()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 1, 0, 12, 1, 0, 0, 0, 0, 4, '2026-07-17T08:00:00', '2026-07-17T08:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
    const auto sourceRootId = source.lastInsertId().toLongLong();
    const auto assetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("new.mp4"));
    QVERIFY(assetId > 0);

    JobEngine jobEngine(&databaseManager);
    BlockingThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    service.startForSourceRoot(sourceRootId);

    const bool thumbnailStarted = thumbnailEngine.waitUntilStarted(10000);
    if (!thumbnailStarted) {
        thumbnailEngine.resume();
    }
    QVERIFY2(thumbnailStarted, "新增素材扫描完成后应立即开始缩略图任务");

    QSqlQuery metadataCount(db);
    const bool metadataQuerySucceeded = metadataCount.exec(QStringLiteral("SELECT COUNT(*) FROM media_metadata"));
    const bool metadataRowAvailable = metadataQuerySucceeded && metadataCount.next();
    const auto metadataRowsBeforeThumbnail = metadataRowAvailable
        ? metadataCount.value(0).toLongLong()
        : qint64{-1};
    metadataCount.finish();
    thumbnailEngine.resume();

    QVERIFY2(metadataQuerySucceeded, qPrintable(metadataCount.lastError().text()));
    QVERIFY(metadataRowAvailable);
    QCOMPARE(metadataRowsBeforeThumbnail, qint64{0});
    QTRY_VERIFY_WITH_TIMEOUT(readThumbnail(db, assetId).first == ThumbnailStatus::Success, 10000);
    QVERIFY2(QFileInfo::exists(readThumbnail(db, assetId).second), "缩略图应在媒体元数据探测前可见");
    QTRY_VERIFY_WITH_TIMEOUT(([&jobEngine]() {
        const auto jobs = jobEngine.jobs();
        return jobs.size() >= 2
            && std::none_of(jobs.cbegin(), jobs.cend(), [](const Job &job) {
                   return job.state == JobState::Running;
               });
    })(), 10000);
    databaseManager.closeProjectDatabase();
}

void MediaTaskServiceRecoveryTest::thumbnailPaginationThrottlesHighFrequencyUpdates()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();

    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Paged', ?, 'ok', 300, 0, 3600, 300, 0, 0, 0, 0, 5, "
        "'2026-07-21T12:00:00', '2026-07-21T12:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
    const auto sourceRootId = source.lastInsertId().toLongLong();
    QVERIFY(sourceRootId > 0);
    for (int index = 0; index < 300; ++index) {
        const auto assetId = insertAsset(
            db,
            sourceRootId,
            sourcePath,
            QStringLiteral("paged-%1.mp4").arg(index, 4, 10, QLatin1Char('0')));
        QVERIFY(assetId > 0);
        insertCompletedMetadata(db, assetId);
    }

    QVERIFY(execSql(db, QStringLiteral(
        "CREATE TABLE job_write_audit (write_count INTEGER NOT NULL DEFAULT 0);")));
    QVERIFY(execSql(db, QStringLiteral(
        "INSERT INTO job_write_audit (write_count) VALUES (0);")));
    QVERIFY(execSql(db, QStringLiteral(
        "CREATE TRIGGER count_job_writes AFTER INSERT ON job "
        "BEGIN UPDATE job_write_audit SET write_count = write_count + 1; END;")));

    auto &telemetry = PerformanceTelemetry::global();
    telemetry.resetForTesting();
    JobEngine jobEngine(&databaseManager);
    FakeThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    QSignalSpy jobsChangedSpy(&jobEngine, &JobEngine::jobsChanged);
    QSignalSpy catalogChangedSpy(&service, &MediaTaskService::mediaCatalogChanged);
    QElapsedTimer elapsed;
    elapsed.start();
    service.startForSourceRoot(sourceRootId);
    service.waitForIdle();
    const auto workerElapsedMs = qMax<qint64>(1, elapsed.elapsed());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QSqlQuery thumbnailCount(db);
    thumbnailCount.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM thumbnail WHERE status = ? AND image_path <> ''"));
    thumbnailCount.addBindValue(static_cast<int>(ThumbnailStatus::Success));
    QVERIFY(thumbnailCount.exec() && thumbnailCount.next());
    QCOMPARE(thumbnailCount.value(0).toLongLong(), qint64{300});
    thumbnailCount.finish();

    const auto peakDepth = telemetry.snapshot()
        .value(QStringLiteral("peak_queue_depths")).toObject()
        .value(QStringLiteral("media.thumbnail_assets")).toInteger();
    QCOMPARE(peakDepth, qint64{128});

    const auto jobs = jobEngine.jobs();
    QCOMPARE(jobs.size(), 1);
    QCOMPARE(jobs.constFirst().type, JobType::Thumbnail);
    QCOMPARE(jobs.constFirst().state, JobState::Completed);
    QCOMPARE(jobs.constFirst().progress, qint64{100});
    QCOMPARE(jobs.constFirst().progressContext.currentItem, qint64{300});
    QCOMPARE(jobs.constFirst().progressContext.totalItems, qint64{300});

    QSqlQuery persistedJob(db);
    QVERIFY(persistedJob.exec(QStringLiteral(
        "SELECT state, progress, progress_context_json FROM job LIMIT 1")));
    QVERIFY(persistedJob.next());
    QCOMPARE(persistedJob.value(0).toInt(), static_cast<int>(JobState::Completed));
    QCOMPARE(persistedJob.value(1).toLongLong(), qint64{100});
    QVERIFY(persistedJob.value(2).toString().contains(QStringLiteral("\"currentItem\":300")));

    QSqlQuery jobWriteCount(db);
    QVERIFY(jobWriteCount.exec(QStringLiteral("SELECT write_count FROM job_write_audit")));
    QVERIFY(jobWriteCount.next());
    const auto persistedWrites = jobWriteCount.value(0).toLongLong();
    const auto maxExpectedWrites = ((workerElapsedMs + 249) / 250) + 4;
    QVERIFY2(persistedWrites <= maxExpectedWrites,
             qPrintable(QStringLiteral("任务写库次数 %1 超过时长 %2ms 对应上界 %3")
                            .arg(persistedWrites).arg(workerElapsedMs).arg(maxExpectedWrites)));
    QCOMPARE(jobsChangedSpy.count(), static_cast<int>(persistedWrites));

    const auto maxExpectedCatalogSignals = ((workerElapsedMs + 499) / 500) + 2;
    QVERIFY(catalogChangedSpy.count() >= 1);
    QVERIFY2(catalogChangedSpy.count() <= maxExpectedCatalogSignals,
             qPrintable(QStringLiteral("目录变化信号 %1 次超过时长 %2ms 对应上界 %3")
                            .arg(catalogChangedSpy.count()).arg(workerElapsedMs).arg(maxExpectedCatalogSignals)));
    databaseManager.closeProjectDatabase();
}

void MediaTaskServiceRecoveryTest::singleThumbnailDatabaseFailureDoesNotAbortRemainingSource()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();
    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 3, 0, 36, 3, 0, 0, 0, 0, 5, "
        "'2026-07-21T12:00:00', '2026-07-21T12:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
    const auto sourceRootId = source.lastInsertId().toLongLong();
    const auto failedAssetId = insertAsset(
        db, sourceRootId, sourcePath, QStringLiteral("fails-to-persist.mp4"));
    const auto crashingAssetId = insertAsset(
        db, sourceRootId, sourcePath, QStringLiteral("tool-crash.mp4"));
    const auto successfulAssetId = insertAsset(
        db, sourceRootId, sourcePath, QStringLiteral("still-runs.mp4"));
    QVERIFY(failedAssetId > 0);
    QVERIFY(crashingAssetId > failedAssetId);
    QVERIFY(successfulAssetId > crashingAssetId);
    insertCompletedMetadata(db, failedAssetId);
    insertCompletedMetadata(db, crashingAssetId);
    insertCompletedMetadata(db, successfulAssetId);

    QSqlQuery failureTrigger(db);
    QVERIFY2(failureTrigger.exec(QStringLiteral(
                 "CREATE TRIGGER reject_one_thumbnail BEFORE INSERT ON thumbnail "
                 "WHEN NEW.asset_id = %1 BEGIN "
                 "SELECT RAISE(FAIL, 'simulated db busy for one asset'); END")
                                     .arg(failedAssetId)),
             qPrintable(failureTrigger.lastError().text()));

    JobEngine jobEngine(&databaseManager);
    ThrowingThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    service.startForSourceRoot(sourceRootId);
    service.waitForIdle();
    QCoreApplication::processEvents();

    QCOMPARE(readThumbnail(db, failedAssetId).first, ThumbnailStatus::Pending);
    QCOMPARE(readThumbnail(db, successfulAssetId).first, ThumbnailStatus::Success);
    QVERIFY(QFileInfo::exists(readThumbnail(db, successfulAssetId).second));
    QCOMPARE(readThumbnail(db, crashingAssetId).first, ThumbnailStatus::Failed);
    const auto jobs = jobEngine.jobs();
    QVERIFY(std::any_of(jobs.cbegin(), jobs.cend(), [](const Job &job) {
        return job.type == JobType::Thumbnail && job.state == JobState::Completed;
    }));
    databaseManager.closeProjectDatabase();
}

void MediaTaskServiceRecoveryTest::recoveryMarkerAuditsMissingCacheReferencesBeforeDispatch()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourcePath));
    const auto databasePath = QDir(tempDir.path()).filePath(QStringLiteral("project.cvdb"));

    DatabaseManager databaseManager;
    QString errorMessage;
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
    auto db = databaseManager.database();
    QSqlQuery source(db);
    source.prepare(QStringLiteral(
        "INSERT INTO source_root "
        "(name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, "
        "other_count, warning_count, scan_version, created_at, updated_at) "
        "VALUES ('Source', ?, 'ok', 1, 0, 12, 1, 0, 0, 0, 0, 5, "
        "'2026-07-21T12:00:00', '2026-07-21T12:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY2(source.exec(), qPrintable(source.lastError().text()));
    const auto sourceRootId = source.lastInsertId().toLongLong();
    const auto assetId = insertAsset(
        db, sourceRootId, sourcePath, QStringLiteral("recover-cache.mp4"));
    QVERIFY(assetId > 0);
    insertCompletedMetadata(db, assetId);
    const auto cachedPath = Paths::projectThumbnailCachePath(
        databasePath, sourceRootId, assetId);
    QVERIFY(QDir().mkpath(QFileInfo(cachedPath).absolutePath()));
    QFile cachedFile(cachedPath);
    QVERIFY(cachedFile.open(QIODevice::WriteOnly));
    cachedFile.write("old-cache");
    cachedFile.close();
    insertThumbnail(db, assetId, ThumbnailStatus::Success, cachedPath);

    const auto eviction = ThumbnailCacheQuota::enforceDirectory(
        Paths::projectThumbnailCacheRoot(databasePath), 0, 0);
    QCOMPARE(eviction.removedFiles, qint64{1});
    QVERIFY(ThumbnailCacheQuota::referenceAuditRequiredForProject(databasePath));

    JobEngine jobEngine(&databaseManager);
    FakeThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, nullptr, &thumbnailEngine);
    service.recoverStaleThumbnails();
    QTRY_VERIFY_WITH_TIMEOUT(readThumbnail(db, assetId).first == ThumbnailStatus::Success, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(readThumbnail(db, assetId).second), 10000);
    QVERIFY(!ThumbnailCacheQuota::referenceAuditRequiredForProject(databasePath));
    service.waitForIdle();
    databaseManager.closeProjectDatabase();
}

QTEST_MAIN(MediaTaskServiceRecoveryTest)

#include "MediaTaskServiceRecoveryTest.moc"
