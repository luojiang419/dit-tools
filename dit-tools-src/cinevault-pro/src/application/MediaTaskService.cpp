#include "application/MediaTaskService.h"

#include "core/jobs/JobEngine.h"
#include "core/media/MediaProbeEngine.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "shared/FolderPathMetadata.h"
#include "shared/Paths.h"
#include "shared/ScopedBackgroundThreadPriority.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QStringList>
#include <QThread>

namespace {
qint64 progressFor(int processed, int total)
{
    if (total <= 0) {
        return 100;
    }
    return qBound<qint64>(qint64{1}, (static_cast<qint64>(processed) * 100) / static_cast<qint64>(total), qint64{100});
}

JobSubject sourceRootSubject(qint64 sourceRootId, const QString &sourceName, const QString &sourcePath)
{
    JobSubject subject;
    subject.kind = QStringLiteral("sourceRoot");
    subject.key = QString::number(sourceRootId);
    subject.name = sourceName;
    subject.path = sourcePath;
    subject.typeLabel = QStringLiteral("素材源");
    return subject;
}

JobProgressContext itemProgressContext(const QString &stepLabel, qint64 current, qint64 total, const QString &unitLabel)
{
    JobProgressContext context;
    context.currentStep = 1;
    context.totalSteps = 1;
    context.stepLabel = stepLabel;
    context.currentItem = current;
    context.totalItems = total;
    context.unitLabel = unitLabel;
    return context;
}

}

MediaTaskService::MediaTaskService(DatabaseManager *databaseManager,
                                   JobEngine *jobEngine,
                                   MediaProbeEngine *mediaProbeEngine,
                                   ThumbnailEngine *thumbnailEngine,
                                   QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_jobEngine(jobEngine)
    , m_mediaProbeEngine(mediaProbeEngine)
    , m_thumbnailEngine(thumbnailEngine)
{
}

MediaTaskService::~MediaTaskService()
{
    waitForIdle();
}

void MediaTaskService::waitForIdle()
{
    m_futures.waitForFinished();
}

void MediaTaskService::startForSourceRoot(qint64 sourceRootId)
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject() || !m_jobEngine || sourceRootId <= 0) {
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT sr.name, sr.path, "
        "(SELECT COUNT(*) FROM asset_file af LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        " WHERE af.source_root_id = sr.id AND af.is_readable = 1 AND af.asset_type IN (?, ?, ?) AND mm.asset_id IS NULL), "
        "(SELECT COUNT(*) FROM asset_file af LEFT JOIN thumbnail th ON th.asset_id = af.id "
        " WHERE af.source_root_id = sr.id AND af.is_readable = 1 AND af.asset_type IN (?, ?) "
        " AND (th.asset_id IS NULL OR th.status <> ? OR COALESCE(th.image_path, '') = '')) "
        "FROM source_root sr WHERE sr.id = ?"));
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Audio));
    query.addBindValue(static_cast<int>(AssetType::Image));
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Image));
    query.addBindValue(static_cast<int>(ThumbnailStatus::Success));
    query.addBindValue(sourceRootId);
    if (!query.exec() || !query.next()) {
        return;
    }

    const auto sourceName = query.value(0).toString();
    const auto sourcePath = query.value(1).toString();
    const auto pendingMetadataCount = query.value(2).toLongLong();
    const auto pendingThumbnailCount = query.value(3).toLongLong();
    const bool needsMetadata = pendingMetadataCount > 0;
    const bool needsThumbnails = pendingThumbnailCount > 0;

    if (!needsMetadata && !needsThumbnails) {
        return;
    }

    const auto projectDatabasePath = m_databaseManager->databaseFilePath();
    const auto activeKey = QStringLiteral("%1|%2")
                               .arg(FolderPathMetadata::normalizedPathKey(projectDatabasePath))
                               .arg(sourceRootId);
    {
        QMutexLocker locker(&m_activeKeysMutex);
        if (m_activeKeys.contains(activeKey)) {
            return;
        }
        m_activeKeys.insert(activeKey);
    }

    qint64 metadataJobId = 0;
    qint64 thumbnailJobId = 0;
    if (needsMetadata) {
        metadataJobId = m_jobEngine->createJob(JobType::Metadata,
                                               QStringLiteral("读取元数据 %1").arg(sourceName),
                                               QStringLiteral("准备读取视频/音频/图片技术参数"),
                                               sourceRootId,
                                               sourceRootSubject(sourceRootId, sourceName, sourcePath),
                                               itemProgressContext(QStringLiteral("读取元数据"), 0, pendingMetadataCount, QStringLiteral("个文件")));
    }
    if (needsThumbnails) {
        thumbnailJobId = m_jobEngine->createJob(JobType::Thumbnail,
                                                QStringLiteral("生成缩略图 %1").arg(sourceName),
                                                QStringLiteral("准备生成视频/图片缩略图"),
                                                sourceRootId,
                                                sourceRootSubject(sourceRootId, sourceName, sourcePath),
                                                itemProgressContext(QStringLiteral("生成缩略图"), 0, pendingThumbnailCount, QStringLiteral("张")));
    }

    auto future = QtConcurrent::run([this, sourceRootId, sourceName, projectDatabasePath, activeKey, metadataJobId, thumbnailJobId]() {
        runMediaJobs(sourceRootId, sourceName, projectDatabasePath, activeKey, metadataJobId, thumbnailJobId);
    });
    m_futures.addFuture(future);
}

void MediaTaskService::recoverStaleThumbnails()
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject() || !m_jobEngine) {
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    if (!query.exec(QStringLiteral(
            "SELECT id FROM source_root "
            "WHERE LOWER(TRIM(COALESCE(status, ''))) IN ('ok', 'warning') "
            "ORDER BY id"))) {
        return;
    }

    QVector<qint64> sourceRootIds;
    while (query.next()) {
        const auto sourceRootId = query.value(0).toLongLong();
        if (sourceRootId > 0) {
            sourceRootIds.append(sourceRootId);
        }
    }

    // A previous crash may happen before the first thumbnail row is created.
    // Reuse the normal pending-work filter to resume both missing and running
    // thumbnail/metadata tasks after the project has settled in the event loop.
    for (const auto sourceRootId : sourceRootIds) {
        QMetaObject::invokeMethod(this, [this, sourceRootId]() {
            startForSourceRoot(sourceRootId);
        }, Qt::QueuedConnection);
    }
}

QVector<AssetFile> MediaTaskService::fetchAssets(QSqlDatabase &db,
                                                 qint64 sourceRootId,
                                                 const QList<AssetType> &assetTypes,
                                                 PendingWork pendingWork,
                                                 QString *errorMessage) const
{
    QVector<AssetFile> assets;
    if (assetTypes.isEmpty()) {
        return assets;
    }

    QStringList placeholders;
    for (qsizetype i = 0; i < assetTypes.size(); ++i) {
        placeholders.append(QStringLiteral("?"));
    }

    const auto join = pendingWork == PendingWork::Metadata
        ? QStringLiteral("LEFT JOIN media_metadata pending ON pending.asset_id = af.id ")
        : QStringLiteral("LEFT JOIN thumbnail pending ON pending.asset_id = af.id ");
    const auto pendingPredicate = pendingWork == PendingWork::Metadata
        ? QStringLiteral("AND pending.asset_id IS NULL ")
        : QStringLiteral("AND (pending.asset_id IS NULL OR pending.status <> %1 OR COALESCE(pending.image_path, '') = '') ")
              .arg(static_cast<int>(ThumbnailStatus::Success));
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, af.name, af.extension, af.absolute_path, af.relative_path, af.parent_path, "
        "af.asset_type, af.size_bytes, af.modified_at, af.is_readable FROM asset_file af %1"
        "WHERE af.source_root_id = ? AND af.is_readable = 1 AND af.asset_type IN (%2) %3ORDER BY af.id")
                      .arg(join, placeholders.join(QStringLiteral(",")), pendingPredicate));
    query.addBindValue(sourceRootId);
    for (const auto assetType : assetTypes) {
        query.addBindValue(static_cast<int>(assetType));
    }

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
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

void MediaTaskService::runMediaJobs(qint64 sourceRootId,
                                    const QString &sourceName,
                                    const QString &projectDatabasePath,
                                    const QString &activeKey,
                                    qint64 metadataJobId,
                                    qint64 thumbnailJobId)
{
    const ScopedBackgroundThreadPriority backgroundPriority;
    const auto activeKeyGuard = qScopeGuard([this, activeKey]() {
        releaseActiveKey(activeKey);
    });
    Q_UNUSED(sourceName);

    const auto connectionName = QStringLiteral("media_%1_%2").arg(sourceRootId).arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
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
        if (metadataJobId > 0) {
            failJob(projectDatabasePath, metadataJobId, errorMessage);
        }
        if (thumbnailJobId > 0) {
            failJob(projectDatabasePath, thumbnailJobId, errorMessage);
        }
        closeConnection();
        return;
    }

    // A usable preview is the first visible result after an import. Generate
    // thumbnails before the slower per-file metadata probe so cards populate
    // progressively instead of staying blank until the whole source is probed.
    if (thumbnailJobId > 0) {
        QString fetchError;
        const auto assets = fetchAssets(db,
                                       sourceRootId,
                                       {AssetType::Video, AssetType::Image},
                                       PendingWork::Thumbnail,
                                       &fetchError);
        if (!fetchError.isEmpty()) {
            failJob(projectDatabasePath, thumbnailJobId, fetchError);
        } else {
            runThumbnailJob(db, projectDatabasePath, sourceRootId, thumbnailJobId, assets);
        }
        notifyCatalogChanged(projectDatabasePath);
    }

    if (metadataJobId > 0) {
        QString fetchError;
        const auto assets = fetchAssets(db,
                                       sourceRootId,
                                       {AssetType::Video, AssetType::Audio, AssetType::Image},
                                       PendingWork::Metadata,
                                       &fetchError);
        if (!fetchError.isEmpty()) {
            failJob(projectDatabasePath, metadataJobId, fetchError);
        } else {
            runMetadataJob(db, projectDatabasePath, metadataJobId, assets);
        }
        notifyCatalogChanged(projectDatabasePath);
    }

    closeConnection();
}

bool MediaTaskService::runMetadataJob(QSqlDatabase &db,
                                      const QString &projectDatabasePath,
                                      qint64 jobId,
                                      const QVector<AssetFile> &assets)
{
    if (!m_mediaProbeEngine) {
        failJob(projectDatabasePath, jobId, QStringLiteral("媒体探测模块未初始化"));
        return false;
    }
    if (assets.isEmpty()) {
        completeJob(projectDatabasePath, jobId, QStringLiteral("没有需要读取元数据的文件"));
        return true;
    }

    int processed = 0;
    int failed = 0;
    for (const auto &asset : assets) {
        const auto result = m_mediaProbeEngine->probe(asset);
        QString persistError;
        if (!persistMediaProbe(db, result, &persistError)) {
            failJob(projectDatabasePath, jobId, QStringLiteral("元数据写入失败：%1").arg(persistError));
            return false;
        }
        if (result.status != ProbeStatus::Success) {
            ++failed;
        }

        ++processed;
        updateJob(projectDatabasePath,
                  jobId,
                  progressFor(processed, assets.size()),
                  QStringLiteral("已读取 %1/%2 个文件，失败 %3 个").arg(processed).arg(assets.size()).arg(failed),
                  itemProgressContext(QStringLiteral("读取元数据"), processed, assets.size(), QStringLiteral("个文件")));
    }

    if (failed == assets.size()) {
        failJob(projectDatabasePath, jobId, QStringLiteral("元数据任务失败：%1 个文件均未成功").arg(failed));
        return false;
    }

    completeJob(projectDatabasePath, jobId, failed > 0
        ? QStringLiteral("元数据读取完成，成功 %1 个，失败 %2 个").arg(assets.size() - failed).arg(failed)
        : QStringLiteral("元数据读取完成，共 %1 个文件").arg(assets.size()));
    return true;
}

bool MediaTaskService::runThumbnailJob(QSqlDatabase &db,
                                       const QString &projectDatabasePath,
                                       qint64 sourceRootId,
                                       qint64 jobId,
                                       const QVector<AssetFile> &assets)
{
    if (!m_thumbnailEngine) {
        failJob(projectDatabasePath, jobId, QStringLiteral("缩略图模块未初始化"));
        return false;
    }
    if (assets.isEmpty()) {
        completeJob(projectDatabasePath, jobId, QStringLiteral("没有需要生成缩略图的文件"));
        return true;
    }

    int processed = 0;
    int failed = 0;
    for (const auto &asset : assets) {
        QString stateError;
        if (!markThumbnailRunning(db, asset.id, &stateError)) {
            failJob(projectDatabasePath, jobId, QStringLiteral("缩略图状态写入失败：%1").arg(stateError));
            return false;
        }
        if (processed == 0) {
            notifyCatalogChanged(projectDatabasePath);
        }

        ThumbnailRequest request;
        request.assetId = asset.id;
        request.sourcePath = asset.absolutePath;
        request.cachePath = Paths::projectThumbnailCachePath(projectDatabasePath, sourceRootId, asset.id);
        request.assetType = asset.assetType;

        const auto result = m_thumbnailEngine->createPlaceholder(request);
        QString persistError;
        if (!persistThumbnail(db, result, &persistError)) {
            failJob(projectDatabasePath, jobId, QStringLiteral("缩略图写入失败：%1").arg(persistError));
            return false;
        }
        if (!result.success) {
            ++failed;
        }

        ++processed;
        updateJob(projectDatabasePath,
                  jobId,
                  progressFor(processed, assets.size()),
                  QStringLiteral("已生成 %1/%2 张缩略图，失败 %3 张").arg(processed).arg(assets.size()).arg(failed),
                  itemProgressContext(QStringLiteral("生成缩略图"), processed, assets.size(), QStringLiteral("张")));
        if ((processed % 6) == 0 || processed == assets.size()) {
            notifyCatalogChanged(projectDatabasePath);
        }
    }

    if (failed == assets.size()) {
        failJob(projectDatabasePath, jobId, QStringLiteral("缩略图任务失败：%1 个文件均未成功").arg(failed));
        return false;
    }

    completeJob(projectDatabasePath, jobId, failed > 0
        ? QStringLiteral("缩略图生成完成，成功 %1 张，失败 %2 张").arg(assets.size() - failed).arg(failed)
        : QStringLiteral("缩略图生成完成，共 %1 张").arg(assets.size()));
    return true;
}

bool MediaTaskService::markThumbnailRunning(QSqlDatabase &db, qint64 assetId, QString *errorMessage) const
{
    if (assetId <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材 ID 无效");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO thumbnail (asset_id, status, image_path, updated_at, error_message) "
        "VALUES (?, ?, '', ?, '')"));
    query.addBindValue(assetId);
    query.addBindValue(static_cast<int>(ThumbnailStatus::Running));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool MediaTaskService::persistMediaProbe(QSqlDatabase &db, const MediaProbeResult &result, QString *errorMessage) const
{
    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    QSqlQuery metadata(db);
    metadata.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO media_metadata "
        "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, raw_json, error_message, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    metadata.addBindValue(result.assetId);
    metadata.addBindValue(static_cast<int>(result.status));
    metadata.addBindValue(static_cast<int>(result.mediaType));
    metadata.addBindValue(result.format.container);
    metadata.addBindValue(result.format.durationMs);
    metadata.addBindValue(result.format.bitRate);
    metadata.addBindValue(result.rawJson);
    metadata.addBindValue(result.errorMessage);
    metadata.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!metadata.exec()) {
        db.rollback();
        if (errorMessage) {
            *errorMessage = metadata.lastError().text();
        }
        return false;
    }

    QSqlQuery clearStreams(db);
    clearStreams.prepare(QStringLiteral("DELETE FROM media_stream WHERE asset_id = ?"));
    clearStreams.addBindValue(result.assetId);
    if (!clearStreams.exec()) {
        db.rollback();
        if (errorMessage) {
            *errorMessage = clearStreams.lastError().text();
        }
        return false;
    }

    if (result.status == ProbeStatus::Success) {
        QSqlQuery streamQuery(db);
        streamQuery.prepare(QStringLiteral(
            "INSERT INTO media_stream "
            "(asset_id, stream_index, stream_kind, codec, bit_rate, width, height, channels, sample_rate) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        for (const auto &stream : result.streams) {
            streamQuery.addBindValue(result.assetId);
            streamQuery.addBindValue(stream.index);
            streamQuery.addBindValue(stream.kind);
            streamQuery.addBindValue(stream.codec);
            streamQuery.addBindValue(stream.bitRate);
            streamQuery.addBindValue(stream.width);
            streamQuery.addBindValue(stream.height);
            streamQuery.addBindValue(stream.channels);
            streamQuery.addBindValue(stream.sampleRate);
            if (!streamQuery.exec()) {
                db.rollback();
                if (errorMessage) {
                    *errorMessage = streamQuery.lastError().text();
                }
                return false;
            }
        }
    }

    if (!db.commit()) {
        db.rollback();
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }
    return true;
}

bool MediaTaskService::persistThumbnail(QSqlDatabase &db, const ThumbnailResult &result, QString *errorMessage) const
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO thumbnail (asset_id, status, image_path, updated_at, error_message) "
        "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(result.assetId);
    query.addBindValue(static_cast<int>(result.success ? ThumbnailStatus::Success : ThumbnailStatus::Failed));
    query.addBindValue(result.success ? result.outputPath : QString());
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(result.errorMessage);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

void MediaTaskService::updateJob(const QString &projectDatabasePath,
                                 qint64 jobId,
                                 qint64 progress,
                                 const QString &detail,
                                 const JobProgressContext &progressContext)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, progress, detail, progressContext]() {
        engine->updateJobForProject(projectDatabasePath, jobId, progress, detail, progressContext);
    }, Qt::QueuedConnection);
}

void MediaTaskService::completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, detail]() {
        engine->completeJobForProject(projectDatabasePath, jobId, detail);
    }, Qt::QueuedConnection);
}

void MediaTaskService::failJob(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, errorMessage]() {
        engine->failJobForProject(projectDatabasePath, jobId, errorMessage);
    }, Qt::QueuedConnection);
}

void MediaTaskService::releaseActiveKey(const QString &activeKey)
{
    QMutexLocker locker(&m_activeKeysMutex);
    m_activeKeys.remove(activeKey);
}

void MediaTaskService::notifyCatalogChanged(const QString &projectDatabasePath)
{
    QMetaObject::invokeMethod(this, [this, projectDatabasePath]() {
        emit mediaCatalogChanged(projectDatabasePath);
    }, Qt::QueuedConnection);
}
