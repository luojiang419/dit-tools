#include "application/MetadataExtractionService.h"

#include "application/IndexingWorkCoordinator.h"
#include "application/JobProgressHeartbeat.h"

#include "core/jobs/JobEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/monitoring/PerformanceTelemetry.h"
#include "shared/FolderPathMetadata.h"
#include "shared/ScopedBackgroundThreadPriority.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>


namespace {
constexpr qsizetype ExifToolBatchSize = 16;
constexpr int ExifToolBatchTimeoutMs = 60000;
constexpr int ExifToolSingleFileTimeoutMs = 20000;

qint64 progressFor(qint64 processed, qint64 total)
{
    return total <= 0
        ? 100
        : qBound<qint64>(qint64{1},
                         (static_cast<qint64>(processed) * 100) / total,
                         qint64{100});
}

qint64 activeProgressFor(qint64 processed, qint64 total)
{
    if (total <= 0) {
        return 100;
    }
    return processed <= 0 ? qint64{1} : progressFor(processed, total);
}

QString assetDisplayName(const AssetFile &asset)
{
    const auto name = asset.name.trimmed();
    if (!name.isEmpty()) {
        return name;
    }
    return QFileInfo(asset.absolutePath).fileName();
}

bool isRetryableBatchFailureMessage(const QString &message)
{
    const auto normalized = message.toCaseFolded();
    return normalized.contains(QStringLiteral("超时"))
        || normalized.contains(QStringLiteral("json 解析失败"))
        || normalized.contains(QStringLiteral("未返回该文件"))
        || normalized.contains(QStringLiteral("退出码"));
}

bool shouldRetryBatchIndividually(const QVector<EmbeddedMetadataResult> &results,
                                  qsizetype batchSize)
{
    if (batchSize <= 1 || results.size() != batchSize) {
        return false;
    }
    QString firstMessage;
    for (const auto &result : results) {
        if (result.status != ProbeStatus::Failed) {
            return false;
        }
        const auto message = result.errorMessage.trimmed();
        if (!isRetryableBatchFailureMessage(message)) {
            return false;
        }
        if (firstMessage.isEmpty()) {
            firstMessage = message;
        } else if (firstMessage != message) {
            return false;
        }
    }
    return !firstMessage.isEmpty();
}

JobProgressContext extractionProgress(qint64 current, qint64 total)
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
    , m_jobHeartbeat(new JobProgressHeartbeat(jobEngine, this))
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

void MetadataExtractionService::setWorkCoordinator(IndexingWorkCoordinator *workCoordinator)
{
    m_workCoordinator = workCoordinator;
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

    m_activeKeys.insert(activeKey);
    const auto jobId = m_jobEngine->createJob(
        JobType::Metadata,
        QStringLiteral("读取真实元数据 %1").arg(sourceName),
        m_exifToolAdapter->isAvailable()
            ? QStringLiteral("准备使用 ExifTool 读取嵌入元数据")
            : m_exifToolAdapter->unavailableReason(),
        sourceRootId,
        sourceSubject(sourceRootId, sourceName, sourcePath),
        extractionProgress(0, 0));

    const auto workGeneration = m_workCoordinator
        ? m_workCoordinator->currentGeneration()
        : quint64{0};
    auto future = QtConcurrent::run([this,
                                     sourceRootId,
                                     projectDatabasePath,
                                     activeKey,
                                     jobId,
                                     workGeneration]() {
        runExtraction(sourceRootId, projectDatabasePath, activeKey, jobId, workGeneration);
    });
    m_futures.addFuture(future);
}

QVector<AssetFile> MetadataExtractionService::fetchPendingAssets(QSqlDatabase &db,
                                                                 qint64 sourceRootId,
                                                                 qint64 lastAssetId,
                                                                 qsizetype limit,
                                                                 QString *errorMessage) const
{
    QVector<AssetFile> assets;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, af.name, af.extension, af.absolute_path, af.relative_path, "
        "af.parent_path, af.asset_type, af.size_bytes, af.modified_at, af.is_readable "
        "FROM asset_file af LEFT JOIN embedded_metadata em ON em.asset_id = af.id "
        "WHERE af.source_root_id = ? AND af.id > ? AND af.asset_type IN (?, ?, ?) AND af.is_readable = 1 "
        "AND (em.asset_id IS NULL OR em.fingerprint_size <> af.size_bytes "
        "OR em.fingerprint_modified <> af.modified_at) ORDER BY af.id LIMIT ?"));
    query.addBindValue(sourceRootId);
    query.addBindValue(lastAssetId);
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Audio));
    query.addBindValue(static_cast<int>(AssetType::Image));
    query.addBindValue(qMax<qsizetype>(1, limit));
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

qint64 MetadataExtractionService::countPendingAssets(QSqlDatabase &db,
                                                     qint64 sourceRootId,
                                                     QString *errorMessage) const
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM asset_file af "
        "LEFT JOIN embedded_metadata em ON em.asset_id = af.id "
        "WHERE af.source_root_id = ? AND af.asset_type IN (?, ?, ?) AND af.is_readable = 1 "
        "AND (em.asset_id IS NULL OR em.fingerprint_size <> af.size_bytes "
        "OR em.fingerprint_modified <> af.modified_at)"));
    query.addBindValue(sourceRootId);
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Audio));
    query.addBindValue(static_cast<int>(AssetType::Image));
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }
    return query.value(0).toLongLong();
}

void MetadataExtractionService::runExtraction(qint64 sourceRootId,
                                              const QString &projectDatabasePath,
                                              const QString &activeKey,
                                              qint64 jobId,
                                              quint64 workGeneration)
{
    const ScopedBackgroundThreadPriority backgroundPriority;
    auto &telemetry = PerformanceTelemetry::global();
    const auto stageStartedAtMs = telemetry.beginStage(
        QStringLiteral("metadata"),
        QStringLiteral("exiftool"),
        {{QStringLiteral("source_root_id"), sourceRootId}});
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

    const auto total = countPendingAssets(db, sourceRootId, &errorMessage);
    if (total < 0 || !errorMessage.isEmpty()) {
        failJob(projectDatabasePath, jobId, errorMessage);
        telemetry.finishStage(
            QStringLiteral("metadata"), QStringLiteral("exiftool"), stageStartedAtMs,
            QStringLiteral("failed"), {{QStringLiteral("error"), errorMessage}});
        closeConnection();
        releaseActiveKey(activeKey);
        return;
    }

    updateJob(projectDatabasePath,
              jobId,
              0,
              QStringLiteral("开始读取 0/%1 个文件").arg(total),
              extractionProgress(0, total));

    constexpr qsizetype PageSize = 128;
    qint64 processed = 0;
    qint64 failed = 0;
    qint64 lastAssetId = 0;
    while (true) {
        const auto assets = fetchPendingAssets(
            db, sourceRootId, lastAssetId, PageSize, &errorMessage);
        if (!errorMessage.isEmpty()) {
            failJob(projectDatabasePath, jobId, errorMessage);
            telemetry.setQueueDepth(QStringLiteral("metadata.exif_assets"), 0);
            telemetry.finishStage(
                QStringLiteral("metadata"), QStringLiteral("exiftool"), stageStartedAtMs,
                QStringLiteral("failed"), {{QStringLiteral("error"), errorMessage}});
            closeConnection();
            releaseActiveKey(activeKey);
            return;
        }
        if (assets.isEmpty()) {
            break;
        }
        telemetry.setQueueDepth(QStringLiteral("metadata.exif_assets"), assets.size());
        lastAssetId = assets.constLast().id;
        for (qsizetype offset = 0; offset < assets.size(); offset += ExifToolBatchSize) {
            const auto count = qMin(ExifToolBatchSize, assets.size() - offset);
            QVector<AssetFile> batch;
            batch.reserve(count);
            for (qsizetype index = 0; index < count; ++index) {
                batch.append(assets.at(offset + index));
            }
            const auto batchFirstItem = qMin<qint64>(processed + 1, total);
            const auto batchLastItem = qMin<qint64>(processed + batch.size(), total);
            const auto activeProgress = activeProgressFor(processed, total);
            const auto activeContext = extractionProgress(batchFirstItem, total);
            const auto batchDetail = batch.size() == 1
                ? QStringLiteral("正在读取 %1/%2 个文件：%3")
                      .arg(batchFirstItem)
                      .arg(total)
                      .arg(assetDisplayName(batch.constFirst()))
                : QStringLiteral("正在读取 %1-%2/%3 个文件：%4 等 %5 个")
                      .arg(batchFirstItem)
                      .arg(batchLastItem)
                      .arg(total)
                      .arg(assetDisplayName(batch.constFirst()))
                      .arg(batch.size());
            QVector<EmbeddedMetadataResult> results;
            {
                IndexingWorkCoordinator::Lease heavyIoLease;
                if (m_workCoordinator) {
                    startJobHeartbeat(projectDatabasePath,
                                      jobId,
                                      activeProgress,
                                      batchDetail,
                                      activeContext,
                                      QStringLiteral("等待执行资源"));
                    const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                        stopJobHeartbeat(jobId);
                    });
                    heavyIoLease = m_workCoordinator->acquire({
                        IndexingWorkCoordinator::Resource::HeavyIo,
                        IndexingWorkCoordinator::Priority::Background,
                        false,
                        workGeneration});
                }
                if (m_workCoordinator && !heavyIoLease) {
                    failJob(projectDatabasePath,
                            jobId,
                            QStringLiteral("真实元数据任务因项目切换、队列拥塞或应用退出而取消"));
                    telemetry.setQueueDepth(QStringLiteral("metadata.exif_assets"), 0);
                    closeConnection();
                    releaseActiveKey(activeKey);
                    return;
                }
                {
                    startJobHeartbeat(projectDatabasePath,
                                      jobId,
                                      activeProgress,
                                      batchDetail,
                                      activeContext,
                                      QStringLiteral("ExifTool 批量读取"));
                    const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                        stopJobHeartbeat(jobId);
                    });
                    results = m_exifToolAdapter->extract(batch, ExifToolBatchTimeoutMs);
                }
                if (shouldRetryBatchIndividually(results, batch.size())) {
                    results.clear();
                    results.reserve(batch.size());
                    updateJob(projectDatabasePath,
                              jobId,
                              activeProgress,
                              QStringLiteral("批量读取异常，正在拆分为单文件读取：%1-%2/%3")
                                  .arg(batchFirstItem)
                                  .arg(batchLastItem)
                                  .arg(total),
                              activeContext);
                    for (qsizetype index = 0; index < batch.size(); ++index) {
                        const auto singleItem = batchFirstItem + index;
                        const auto singleContext = extractionProgress(singleItem, total);
                        const auto singleDetail = QStringLiteral("正在单文件读取 %1/%2 个文件：%3")
                                                      .arg(singleItem)
                                                      .arg(total)
                                                      .arg(assetDisplayName(batch.at(index)));
                        startJobHeartbeat(projectDatabasePath,
                                          jobId,
                                          activeProgress,
                                          singleDetail,
                                          singleContext,
                                          QStringLiteral("ExifTool 单文件读取"));
                        const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                            stopJobHeartbeat(jobId);
                        });
                        const QVector<AssetFile> singleBatch{batch.at(index)};
                        const auto singleResults = m_exifToolAdapter->extract(
                            singleBatch, ExifToolSingleFileTimeoutMs);
                        results += singleResults;
                    }
                }
            }
            IndexingWorkCoordinator::Lease writerLease;
            if (m_workCoordinator) {
                startJobHeartbeat(projectDatabasePath,
                                  jobId,
                                  activeProgress,
                                  batchDetail,
                                  activeContext,
                                  QStringLiteral("等待写入资源"));
                const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                    stopJobHeartbeat(jobId);
                });
                writerLease = m_workCoordinator->acquire({
                    IndexingWorkCoordinator::Resource::SqliteWriter,
                    IndexingWorkCoordinator::Priority::Background,
                    false,
                    workGeneration});
            }
            if (m_workCoordinator && !writerLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("真实元数据写入因项目切换、队列拥塞或应用退出而取消"));
                telemetry.setQueueDepth(QStringLiteral("metadata.exif_assets"), 0);
                closeConnection();
                releaseActiveKey(activeKey);
                return;
            }
            if (!persistBatch(db, results, &errorMessage)) {
                failJob(projectDatabasePath, jobId, QStringLiteral("真实元数据写入失败：%1").arg(errorMessage));
                telemetry.setQueueDepth(QStringLiteral("metadata.exif_assets"), 0);
                telemetry.finishStage(
                    QStringLiteral("metadata"), QStringLiteral("exiftool"), stageStartedAtMs,
                    QStringLiteral("failed"), {{QStringLiteral("error"), errorMessage}});
                closeConnection();
                releaseActiveKey(activeKey);
                return;
            }
            for (const auto &result : results) {
                if (result.status != ProbeStatus::Success) {
                    ++failed;
                }
            }
            processed += batch.size();
            updateJob(projectDatabasePath,
                      jobId,
                      progressFor(processed, total),
                      QStringLiteral("已读取 %1/%2 个文件，失败 %3 个")
                          .arg(processed).arg(total).arg(failed),
                      extractionProgress(processed, total));
        }
        telemetry.setQueueDepth(QStringLiteral("metadata.exif_assets"), 0);
    }

    if (processed == 0) {
        completeJob(projectDatabasePath, jobId, QStringLiteral("真实元数据已是最新状态"));
    } else if (failed == processed) {
        failJob(projectDatabasePath, jobId, QStringLiteral("ExifTool 未能读取任何文件，请检查运行时或文件权限"));
    } else {
        completeJob(projectDatabasePath,
                    jobId,
                    failed > 0
                        ? QStringLiteral("真实元数据读取完成：成功 %1 个，失败 %2 个")
                              .arg(processed - failed).arg(failed)
                        : QStringLiteral("真实元数据读取完成，共 %1 个文件").arg(processed));
    }
    telemetry.finishStage(
        QStringLiteral("metadata"),
        QStringLiteral("exiftool"),
        stageStartedAtMs,
        failed == processed && processed > 0 ? QStringLiteral("failed") : QStringLiteral("ok"),
        {{QStringLiteral("processed"), processed}, {QStringLiteral("failed"), failed}});
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

void MetadataExtractionService::startJobHeartbeat(const QString &projectDatabasePath,
                                                  qint64 jobId,
                                                  qint64 progress,
                                                  const QString &detailPrefix,
                                                  const JobProgressContext &context,
                                                  const QString &waitLabel)
{
    if (!m_jobHeartbeat || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobHeartbeat,
                              [heartbeat = m_jobHeartbeat,
                               projectDatabasePath,
                               jobId,
                               progress,
                               detailPrefix,
                               context,
                               waitLabel]() {
        heartbeat->start(projectDatabasePath,
                         jobId,
                         progress,
                         detailPrefix,
                         context,
                         waitLabel);
    },
                              Qt::QueuedConnection);
}

void MetadataExtractionService::stopJobHeartbeat(qint64 jobId)
{
    if (!m_jobHeartbeat || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobHeartbeat,
                              [heartbeat = m_jobHeartbeat, jobId]() {
        heartbeat->stop(jobId);
    },
                              Qt::QueuedConnection);
}

void MetadataExtractionService::completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    stopJobHeartbeat(jobId);
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, detail]() {
        engine->completeJobForProject(projectDatabasePath, jobId, detail);
    }, Qt::QueuedConnection);
}

void MetadataExtractionService::failJob(const QString &projectDatabasePath, qint64 jobId, const QString &message)
{
    stopJobHeartbeat(jobId);
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
