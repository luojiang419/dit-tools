#include "application/MetadataExtractionService.h"

#include "core/jobs/JobEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "shared/FolderPathMetadata.h"
#include "shared/ScopedBackgroundThreadPriority.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>


namespace {
qint64 progressFor(int processed, int total)
{
    return total <= 0
        ? 100
        : qBound<qint64>(qint64{1},
                         (static_cast<qint64>(processed) * 100) / total,
                         qint64{100});
}

JobProgressContext extractionProgress(int current, int total)
{
    JobProgressContext context;
    context.currentStep = 1;
    context.totalSteps = 1;
    context.stepLabel = QStringLiteral("读取嵌入元数据");
    context.currentItem = current;
    context.totalItems = total;
    context.unitLabel = QStringLiteral("个文件");
    return context;
}

JobSubject sourceSubject(qint64 sourceRootId, const QString &name, const QString &path)
{
    JobSubject subject;
    subject.kind = QStringLiteral("sourceRoot");
    subject.key = QString::number(sourceRootId);
    subject.name = name;
    subject.path = path;
    subject.typeLabel = QStringLiteral("素材源");
    return subject;
}

}

MetadataExtractionService::MetadataExtractionService(DatabaseManager *databaseManager,
                                                     JobEngine *jobEngine,
                                                     ExifToolAdapter *exifToolAdapter,
                                                     QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_jobEngine(jobEngine)
    , m_exifToolAdapter(exifToolAdapter)
{
}

MetadataExtractionService::~MetadataExtractionService()
{
    waitForIdle();
}

void MetadataExtractionService::waitForIdle()
{
    m_futures.waitForFinished();
}

void MetadataExtractionService::startForSourceRoot(qint64 sourceRootId)
{
    if (!m_databaseManager
        || !m_databaseManager->hasOpenProject()
        || !m_jobEngine
        || !m_exifToolAdapter
        || sourceRootId <= 0) {
        return;
    }

    QSqlQuery source(m_databaseManager->database());
    source.prepare(QStringLiteral("SELECT name, path FROM source_root WHERE id = ?"));
    source.addBindValue(sourceRootId);
    if (!source.exec() || !source.next()) {
        return;
    }
    const auto sourceName = source.value(0).toString();
    const auto sourcePath = source.value(1).toString();
    const auto projectDatabasePath = m_databaseManager->databaseFilePath();
    const auto activeKey = QStringLiteral("%1|%2")
                               .arg(FolderPathMetadata::normalizedPathKey(projectDatabasePath))
                               .arg(sourceRootId);
    if (m_activeKeys.contains(activeKey)) {
        return;
    }

    QString fetchError;
    auto projectDb = m_databaseManager->database();
    const auto pendingAssets = fetchPendingAssets(projectDb,
                                                  sourceRootId,
                                                  &fetchError);
    if (!fetchError.isEmpty() || pendingAssets.isEmpty()) {
        return;
    }

    m_activeKeys.insert(activeKey);
    const auto jobId = m_jobEngine->createJob(
        JobType::Metadata,
        QStringLiteral("读取真实元数据 %1").arg(sourceName),
        m_exifToolAdapter->isAvailable()
            ? QStringLiteral("准备使用 ExifTool 读取嵌入元数据")
            : m_exifToolAdapter->unavailableReason(),
        sourceRootId,
        sourceSubject(sourceRootId, sourceName, sourcePath),
        extractionProgress(0, pendingAssets.size()));

    auto future = QtConcurrent::run([this,
                                     sourceRootId,
                                     projectDatabasePath,
                                     activeKey,
                                     jobId]() {
        runExtraction(sourceRootId, projectDatabasePath, activeKey, jobId);
    });
    m_futures.addFuture(future);
}

QVector<AssetFile> MetadataExtractionService::fetchPendingAssets(QSqlDatabase &db,
                                                                 qint64 sourceRootId,
                                                                 QString *errorMessage) const
{
    QVector<AssetFile> assets;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, af.name, af.extension, af.absolute_path, af.relative_path, "
        "af.parent_path, af.asset_type, af.size_bytes, af.modified_at, af.is_readable "
        "FROM asset_file af LEFT JOIN embedded_metadata em ON em.asset_id = af.id "
        "WHERE af.source_root_id = ? AND af.asset_type IN (?, ?, ?) AND af.is_readable = 1 "
        "AND (em.asset_id IS NULL OR em.fingerprint_size <> af.size_bytes "
        "OR em.fingerprint_modified <> af.modified_at) ORDER BY af.id"));
    query.addBindValue(sourceRootId);
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Audio));
    query.addBindValue(static_cast<int>(AssetType::Image));
    if (!query.exec()) {
        if (errorMessage) *errorMessage = query.lastError().text();
        return assets;
    }
    while (query.next()) {
        AssetFile asset;
        asset.id = query.value(0).toLongLong();
        asset.sourceRootId = query.value(1).toLongLong();
        asset.name = query.value(2).toString();
        asset.extension = query.value(3).toString();
        asset.absolutePath = query.value(4).toString();
        asset.relativePath = query.value(5).toString();
        asset.parentPath = query.value(6).toString();
        asset.assetType = static_cast<AssetType>(query.value(7).toInt());
        asset.sizeBytes = query.value(8).toLongLong();
        asset.modifiedAt = query.value(9).toString();
        asset.readable = query.value(10).toInt() == 1;
        assets.append(asset);
    }
    return assets;
}

void MetadataExtractionService::runExtraction(qint64 sourceRootId,
                                              const QString &projectDatabasePath,
                                              const QString &activeKey,
                                              qint64 jobId)
{
    const ScopedBackgroundThreadPriority backgroundPriority;
    const auto connectionName = QStringLiteral("embedded_metadata_%1_%2")
                                    .arg(sourceRootId)
                                    .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString errorMessage;
    auto db = m_databaseManager->openThreadConnectionForPath(projectDatabasePath,
                                                              connectionName,
                                                              &errorMessage);
    const auto closeConnection = [&]() {
        db.close();
        db = QSqlDatabase();
        m_databaseManager->closeThreadConnection(connectionName);
    };
    if (!db.isOpen()) {
        failJob(projectDatabasePath, jobId, errorMessage);
        closeConnection();
        releaseActiveKey(activeKey);
        return;
    }

    const auto assets = fetchPendingAssets(db, sourceRootId, &errorMessage);
    if (!errorMessage.isEmpty()) {
        failJob(projectDatabasePath, jobId, errorMessage);
        closeConnection();
        releaseActiveKey(activeKey);
        return;
    }

    constexpr qsizetype kBatchSize = 32;
    int processed = 0;
    int failed = 0;
    for (qsizetype offset = 0; offset < assets.size(); offset += kBatchSize) {
        const auto count = qMin(kBatchSize, assets.size() - offset);
        QVector<AssetFile> batch;
        batch.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            batch.append(assets.at(offset + index));
        }
        const auto results = m_exifToolAdapter->extract(batch);
        if (!persistBatch(db, results, &errorMessage)) {
            failJob(projectDatabasePath, jobId, QStringLiteral("真实元数据写入失败：%1").arg(errorMessage));
            closeConnection();
            releaseActiveKey(activeKey);
            return;
        }
        for (const auto &result : results) {
            if (result.status != ProbeStatus::Success) ++failed;
        }
        processed += batch.size();
        updateJob(projectDatabasePath,
                  jobId,
                  progressFor(processed, assets.size()),
                  QStringLiteral("已读取 %1/%2 个文件，失败 %3 个")
                      .arg(processed).arg(assets.size()).arg(failed),
                  extractionProgress(processed, assets.size()));
    }

    if (assets.isEmpty()) {
        completeJob(projectDatabasePath, jobId, QStringLiteral("真实元数据已是最新状态"));
    } else if (failed == assets.size()) {
        failJob(projectDatabasePath, jobId, QStringLiteral("ExifTool 未能读取任何文件，请检查运行时或文件权限"));
    } else {
        completeJob(projectDatabasePath,
                    jobId,
                    failed > 0
                        ? QStringLiteral("真实元数据读取完成：成功 %1 个，失败 %2 个")
                              .arg(assets.size() - failed).arg(failed)
                        : QStringLiteral("真实元数据读取完成，共 %1 个文件").arg(assets.size()));
    }
    closeConnection();
    QMetaObject::invokeMethod(this, [this, projectDatabasePath]() {
        emit metadataCatalogChanged(projectDatabasePath);
    }, Qt::QueuedConnection);
    releaseActiveKey(activeKey);
}

bool MetadataExtractionService::persistBatch(QSqlDatabase &db,
                                             const QVector<EmbeddedMetadataResult> &results,
                                             QString *errorMessage) const
{
    if (!db.transaction()) {
        if (errorMessage) *errorMessage = db.lastError().text();
        return false;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO embedded_metadata ("
        "asset_id, status, tool_version, fingerprint_size, fingerprint_modified, capture_time, create_time, "
        "camera_make, camera_model, lens_model, camera_serial_hash, gps_latitude, gps_longitude, gps_altitude, "
        "orientation, width, height, duration_ms, frame_rate, video_codec, color_space, sample_rate, channels, "
        "bit_rate, timecode, title, description, artist, album, genre, keywords, search_text, raw_json, "
        "error_message, updated_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const auto &result : results) {
        query.addBindValue(result.assetId);
        query.addBindValue(static_cast<int>(result.status));
        query.addBindValue(result.toolVersion);
        query.addBindValue(result.fingerprintSize);
        query.addBindValue(result.fingerprintModified);
        query.addBindValue(result.captureTime);
        query.addBindValue(result.createTime);
        query.addBindValue(result.cameraMake);
        query.addBindValue(result.cameraModel);
        query.addBindValue(result.lensModel);
        query.addBindValue(result.cameraSerialHash);
        query.addBindValue(result.gpsLatitude ? QVariant(*result.gpsLatitude) : QVariant());
        query.addBindValue(result.gpsLongitude ? QVariant(*result.gpsLongitude) : QVariant());
        query.addBindValue(result.gpsAltitude ? QVariant(*result.gpsAltitude) : QVariant());
        query.addBindValue(result.orientation);
        query.addBindValue(result.width);
        query.addBindValue(result.height);
        query.addBindValue(result.durationMs);
        query.addBindValue(result.frameRate);
        query.addBindValue(result.videoCodec);
        query.addBindValue(result.colorSpace);
        query.addBindValue(result.sampleRate);
        query.addBindValue(result.channels);
        query.addBindValue(result.bitRate);
        query.addBindValue(result.timecode);
        query.addBindValue(result.title);
        query.addBindValue(result.description);
        query.addBindValue(result.artist);
        query.addBindValue(result.album);
        query.addBindValue(result.genre);
        query.addBindValue(result.keywords);
        query.addBindValue(result.searchText);
        query.addBindValue(result.rawJson);
        query.addBindValue(result.errorMessage);
        query.addBindValue(now);
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

void MetadataExtractionService::updateJob(const QString &projectDatabasePath,
                                          qint64 jobId,
                                          qint64 progress,
                                          const QString &detail,
                                          const JobProgressContext &context)
{
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, progress, detail, context]() {
        engine->updateJobForProject(projectDatabasePath, jobId, progress, detail, context);
    }, Qt::QueuedConnection);
}

void MetadataExtractionService::completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, detail]() {
        engine->completeJobForProject(projectDatabasePath, jobId, detail);
    }, Qt::QueuedConnection);
}

void MetadataExtractionService::failJob(const QString &projectDatabasePath, qint64 jobId, const QString &message)
{
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, message]() {
        engine->failJobForProject(projectDatabasePath, jobId, message);
    }, Qt::QueuedConnection);
}

void MetadataExtractionService::releaseActiveKey(const QString &activeKey)
{
    QMetaObject::invokeMethod(this, [this, activeKey]() {
        m_activeKeys.remove(activeKey);
    }, Qt::QueuedConnection);
}
