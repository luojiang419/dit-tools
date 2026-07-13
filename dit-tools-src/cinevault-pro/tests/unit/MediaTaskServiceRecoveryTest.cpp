#include "application/MediaTaskService.h"
#include "core/jobs/JobEngine.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

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
        "VALUES ('Source', ?, 'ok', 2, 0, 24, 2, 0, 0, 0, 0, 2, '2026-07-06T12:00:00', '2026-07-06T12:00:00')"));
    source.addBindValue(sourcePath);
    QVERIFY(source.exec());
    const auto sourceRootId = source.lastInsertId().toLongLong();
    QVERIFY(sourceRootId > 0);

    const auto staleAssetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("stale.mp4"));
    const auto finishedAssetId = insertAsset(db, sourceRootId, sourcePath, QStringLiteral("finished.mp4"));
    QVERIFY(staleAssetId > 0);
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

    const auto finishedRow = readThumbnail(db, finishedAssetId);
    QCOMPARE(finishedRow.first, ThumbnailStatus::Success);
    QCOMPARE(finishedRow.second, finishedPath);

    QTRY_VERIFY_WITH_TIMEOUT(!jobEngine.jobs().isEmpty() && jobEngine.jobs().first().state == JobState::Completed, 10000);
    databaseManager.closeProjectDatabase();
}

QTEST_MAIN(MediaTaskServiceRecoveryTest)

#include "MediaTaskServiceRecoveryTest.moc"
