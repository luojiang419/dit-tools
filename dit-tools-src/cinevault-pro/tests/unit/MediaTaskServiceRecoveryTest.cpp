#include "application/MediaTaskService.h"
#include "core/jobs/JobEngine.h"
#include "core/media/MediaProbeEngine.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>

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
    MediaProbeEngine mediaProbeEngine(nullptr);
    BlockingThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, &mediaProbeEngine, &thumbnailEngine);
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
    MediaProbeEngine mediaProbeEngine(nullptr);
    BlockingThumbnailEngine thumbnailEngine;
    MediaTaskService service(&databaseManager, &jobEngine, &mediaProbeEngine, &thumbnailEngine);
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

QTEST_MAIN(MediaTaskServiceRecoveryTest)

#include "MediaTaskServiceRecoveryTest.moc"
