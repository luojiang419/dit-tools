#include "application/MediaTaskService.h"

#include "application/IndexingWorkCoordinator.h"

#include "core/jobs/JobEngine.h"
#include "core/media/MediaProbeEngine.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/logging/Logger.h"
#include "infrastructure/monitoring/PerformanceTelemetry.h"
#include "shared/FolderPathMetadata.h"
#include "shared/Paths.h"
#include "shared/ScopedBackgroundThreadPriority.h"
#include "shared/ThumbnailCacheQuota.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QStringList>
#include <QThread>

#include <utility>
#include <exception>

namespace {
constexpr qint64 ProgressMinPublishIntervalMs = 250;
constexpr qint64 ProgressMaxPublishIntervalMs = 500;
constexpr int CatalogPublishIntervalMs = 500;

qint64 progressFor(qint64 processed, qint64 total)
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

class ProgressPublishGate final {
public:
    bool shouldPublish(qint64 currentItem, qint64 progress, bool force = false)
    {
        if (currentItem == m_lastPublishedItem) {
            return false;
        }

        const auto elapsedMs = m_clock.isValid() ? m_clock.elapsed() : ProgressMaxPublishIntervalMs;
        const bool progressChanged = progress != m_lastPublishedProgress;
        if (!force
            && elapsedMs < ProgressMaxPublishIntervalMs
            && (!progressChanged || elapsedMs < ProgressMinPublishIntervalMs)) {
            return false;
        }

        m_lastPublishedItem = currentItem;
        m_lastPublishedProgress = progress;
        m_clock.restart();
        return true;
    }

private:
    QElapsedTimer m_clock;
    qint64 m_lastPublishedItem = -1;
    qint64 m_lastPublishedProgress = -1;
};

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
    m_catalogChangeTimer.setSingleShot(true);
    m_catalogChangeTimer.setInterval(CatalogPublishIntervalMs);
    connect(&m_catalogChangeTimer, &QTimer::timeout, this, [this]() {
        publishPendingCatalogChanges();
    });
}

MediaTaskService::~MediaTaskService()
{
    waitForIdle();
}

void MediaTaskService::waitForIdle()
{
    m_futures.waitForFinished();
}

void MediaTaskService::setWorkCoordinator(IndexingWorkCoordinator *workCoordinator)
{
    m_workCoordinator = workCoordinator;
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

    const auto workGeneration = m_workCoordinator
        ? m_workCoordinator->currentGeneration()
        : quint64{0};
    auto future = QtConcurrent::run([this, sourceRootId, sourceName, projectDatabasePath, activeKey,
                                     metadataJobId, thumbnailJobId, pendingMetadataCount,
                                     pendingThumbnailCount, workGeneration]() {
        runMediaJobs(sourceRootId,
                     sourceName,
                     projectDatabasePath,
                     activeKey,
                     metadataJobId,
                     thumbnailJobId,
                     pendingMetadataCount,
                     pendingThumbnailCount,
                     workGeneration);
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

    const auto projectDatabasePath = m_databaseManager->databaseFilePath();
    if (ThumbnailCacheQuota::referenceAuditRequiredForProject(projectDatabasePath)) {
        const auto workGeneration = m_workCoordinator
            ? m_workCoordinator->currentGeneration()
            : quint64{0};
        auto future = QtConcurrent::run([this,
                                         projectDatabasePath,
                                         sourceRootIds,
                                         workGeneration]() {
            const auto connectionName = QStringLiteral("thumbnail_recovery_audit_%1")
                                            .arg(reinterpret_cast<quintptr>(
                                                QThread::currentThreadId()));
            QString errorMessage;
            auto db = m_databaseManager->openThreadConnectionForPath(
                projectDatabasePath, connectionName, &errorMessage);
            bool repaired = false;
            if (db.isOpen()) {
                IndexingWorkCoordinator::Lease writerLease;
                if (m_workCoordinator) {
                    writerLease = m_workCoordinator->acquire({
                        IndexingWorkCoordinator::Resource::SqliteWriter,
                        IndexingWorkCoordinator::Priority::Background,
                        false,
                        workGeneration});
                }
                if (!m_workCoordinator || writerLease) {
                    ThumbnailCacheQuotaResult audit;
                    audit.requiresReferenceAudit = true;
                    repaired = invalidateEvictedThumbnails(db, audit, &errorMessage);
                } else {
                    errorMessage = QStringLiteral("项目切换或应用退出，恢复审计已延后");
                }
                db.close();
            }
            db = QSqlDatabase();
            m_databaseManager->closeThreadConnection(connectionName);
            if (!repaired) {
                Logger::warn(QStringLiteral("启动时修复缩略图缓存引用失败：%1")
                                 .arg(errorMessage));
                return;
            }
            ThumbnailCacheQuota::completeReferenceAuditForProject(projectDatabasePath);
            QMetaObject::invokeMethod(this,
                                      [this, projectDatabasePath, sourceRootIds]() {
                if (!m_databaseManager
                    || FolderPathMetadata::normalizedPathKey(
                           m_databaseManager->databaseFilePath())
                        != FolderPathMetadata::normalizedPathKey(projectDatabasePath)) {
                    return;
                }
                for (const auto sourceRootId : sourceRootIds) {
                    startForSourceRoot(sourceRootId);
                }
            },
                                      Qt::QueuedConnection);
        });
        m_futures.addFuture(future);
        return;
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
                                                 qint64 lastAssetId,
                                                 qsizetype limit,
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
        "WHERE af.source_root_id = ? AND af.id > ? AND af.is_readable = 1 "
        "AND af.asset_type IN (%2) %3ORDER BY af.id LIMIT ?")
                      .arg(join, placeholders.join(QStringLiteral(",")), pendingPredicate));
    query.addBindValue(sourceRootId);
    query.addBindValue(lastAssetId);
    for (const auto assetType : assetTypes) {
        query.addBindValue(static_cast<int>(assetType));
    }
    query.addBindValue(qMax<qsizetype>(1, limit));

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
                                    qint64 thumbnailJobId,
                                    qint64 metadataTotal,
                                    qint64 thumbnailTotal,
                                    quint64 workGeneration)
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
        runThumbnailJob(
            db, projectDatabasePath, sourceRootId, thumbnailJobId, thumbnailTotal, workGeneration);
        flushCatalogChanged(projectDatabasePath);
    }

    if (metadataJobId > 0) {
        runMetadataJob(
            db, projectDatabasePath, metadataJobId, sourceRootId, metadataTotal, workGeneration);
        flushCatalogChanged(projectDatabasePath);
    }

    closeConnection();
}

bool MediaTaskService::runMetadataJob(QSqlDatabase &db,
                                      const QString &projectDatabasePath,
                                      qint64 jobId,
                                      qint64 sourceRootId,
                                      qint64 total,
                                      quint64 workGeneration)
{
    if (!m_mediaProbeEngine) {
        failJob(projectDatabasePath, jobId, QStringLiteral("媒体探测模块未初始化"));
        return false;
    }
    if (total <= 0) {
        completeJob(projectDatabasePath, jobId, QStringLiteral("没有需要读取元数据的文件"));
        return true;
    }

    constexpr qsizetype PageSize = 128;
    auto &telemetry = PerformanceTelemetry::global();
    qint64 processed = 0;
    qint64 failed = 0;
    qint64 lastAssetId = 0;
    ProgressPublishGate progressGate;
    while (true) {
        QString fetchError;
        const auto assets = fetchAssets(
            db,
            sourceRootId,
            {AssetType::Video, AssetType::Audio, AssetType::Image},
            PendingWork::Metadata,
            lastAssetId,
            PageSize,
            &fetchError);
        if (!fetchError.isEmpty()) {
            telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), 0);
            failJob(projectDatabasePath, jobId, fetchError);
            return false;
        }
        if (assets.isEmpty()) {
            break;
        }
        IndexingWorkCoordinator::Lease heavyIoLease;
        if (m_workCoordinator) {
            heavyIoLease = m_workCoordinator->acquire({
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Background,
                true,
                workGeneration});
            if (!heavyIoLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("元数据任务因项目切换、队列拥塞或应用退出而取消"));
                return false;
            }
        }
        telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), assets.size());
        lastAssetId = assets.constLast().id;
        for (const auto &asset : assets) {
            MediaProbeResult result;
            result.assetId = asset.id;
            try {
                result = m_mediaProbeEngine->probe(asset);
            } catch (const std::exception &exception) {
                result.status = ProbeStatus::Failed;
                result.errorMessage = QStringLiteral("外部媒体探测异常：%1")
                                          .arg(QString::fromUtf8(exception.what()));
            } catch (...) {
                result.status = ProbeStatus::Failed;
                result.errorMessage = QStringLiteral("外部媒体探测发生未知异常");
            }
            QString persistError;
            IndexingWorkCoordinator::Lease writerLease;
            if (m_workCoordinator) {
                writerLease = m_workCoordinator->acquire({
                    IndexingWorkCoordinator::Resource::SqliteWriter,
                    IndexingWorkCoordinator::Priority::Background,
                    false,
                    workGeneration});
            }
            if (m_workCoordinator && !writerLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("元数据写入因项目切换、队列拥塞或应用退出而取消"));
                return false;
            }
            if (!persistMediaProbe(db, result, &persistError)) {
                Logger::warn(QStringLiteral(
                    "单文件元数据写入失败，已跳过并继续：asset_id=%1 error=%2")
                                 .arg(asset.id)
                                 .arg(persistError));
                ++failed;
            } else if (result.status != ProbeStatus::Success) {
                ++failed;
            }

            ++processed;
            const auto progress = progressFor(processed, total);
            if (progressGate.shouldPublish(processed, progress)) {
                updateJob(projectDatabasePath,
                          jobId,
                          progress,
                          QStringLiteral("已读取 %1/%2 个文件，失败 %3 个")
                              .arg(processed).arg(total).arg(failed),
                          itemProgressContext(
                              QStringLiteral("读取元数据"), processed, total, QStringLiteral("个文件")));
            }
        }
        telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), 0);
    }

    const auto finalProgress = progressFor(processed, total);
    if (progressGate.shouldPublish(processed, finalProgress, true)) {
        updateJob(projectDatabasePath,
                  jobId,
                  finalProgress,
                  QStringLiteral("已读取 %1/%2 个文件，失败 %3 个")
                      .arg(processed).arg(total).arg(failed),
                  itemProgressContext(
                      QStringLiteral("读取元数据"), processed, total, QStringLiteral("个文件")));
    }

    if (processed > 0 && failed == processed) {
        failJob(projectDatabasePath, jobId, QStringLiteral("元数据任务失败：%1 个文件均未成功").arg(failed));
        return false;
    }

    completeJob(projectDatabasePath, jobId, failed > 0
        ? QStringLiteral("元数据读取完成，成功 %1 个，失败 %2 个").arg(processed - failed).arg(failed)
        : QStringLiteral("元数据读取完成，共 %1 个文件").arg(processed));
    return true;
}

bool MediaTaskService::runThumbnailJob(QSqlDatabase &db,
                                       const QString &projectDatabasePath,
                                       qint64 sourceRootId,
                                       qint64 jobId,
                                       qint64 total,
                                       quint64 workGeneration)
{
    if (!m_thumbnailEngine) {
        failJob(projectDatabasePath, jobId, QStringLiteral("缩略图模块未初始化"));
        return false;
    }
    if (total <= 0) {
        completeJob(projectDatabasePath, jobId, QStringLiteral("没有需要生成缩略图的文件"));
        return true;
    }

    constexpr qsizetype PageSize = 128;
    auto &telemetry = PerformanceTelemetry::global();
    qint64 processed = 0;
    qint64 failed = 0;
    qint64 lastAssetId = 0;
    ProgressPublishGate progressGate;
    QElapsedTimer catalogPublishClock;
    while (true) {
        QString fetchError;
        const auto assets = fetchAssets(
            db,
            sourceRootId,
            {AssetType::Video, AssetType::Image},
            PendingWork::Thumbnail,
            lastAssetId,
            PageSize,
            &fetchError);
        if (!fetchError.isEmpty()) {
            telemetry.setQueueDepth(QStringLiteral("media.thumbnail_assets"), 0);
            failJob(projectDatabasePath, jobId, fetchError);
            return false;
        }
        if (assets.isEmpty()) {
            break;
        }
        IndexingWorkCoordinator::Lease heavyIoLease;
        if (m_workCoordinator) {
            heavyIoLease = m_workCoordinator->acquire({
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Foreground,
                false,
                workGeneration});
            if (!heavyIoLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("缩略图任务因项目切换、队列拥塞或应用退出而取消"));
                return false;
            }
        }
        telemetry.setQueueDepth(QStringLiteral("media.thumbnail_assets"), assets.size());
        lastAssetId = assets.constLast().id;
        constexpr qint64 EstimatedThumbnailBytes = 4LL * 1024LL * 1024LL;
        auto quota = ThumbnailCacheQuota::enforceForProject(projectDatabasePath,
                                                            EstimatedThumbnailBytes);
        for (const auto &quotaError : std::as_const(quota.errors)) {
            Logger::warn(quotaError);
        }
        const auto invalidateQuotaReferences = [&](const ThumbnailCacheQuotaResult &quotaResult,
                                                   QString *quotaError) {
            IndexingWorkCoordinator::Lease writerLease;
            if (m_workCoordinator) {
                writerLease = m_workCoordinator->acquire({
                    IndexingWorkCoordinator::Resource::SqliteWriter,
                    IndexingWorkCoordinator::Priority::Foreground,
                    false,
                    workGeneration});
            }
            if (m_workCoordinator && !writerLease) {
                if (quotaError) {
                    *quotaError = QStringLiteral("项目切换或应用退出，引用修复留待下次恢复");
                }
                return false;
            }
            return invalidateEvictedThumbnails(db, quotaResult, quotaError);
        };
        QString quotaReferenceError;
        if (!quota.removedPaths.isEmpty() || quota.requiresReferenceAudit) {
            if (!invalidateQuotaReferences(quota, &quotaReferenceError)) {
                Logger::warn(QStringLiteral("更新已回收缩略图引用失败：%1")
                                 .arg(quotaReferenceError));
            } else {
                ThumbnailCacheQuota::completeReferenceAuditForProject(projectDatabasePath);
            }
        }
        qint64 trackedCacheBytes = quota.bytesAfter;
        for (const auto &asset : assets) {
            QString stateError;
            {
                IndexingWorkCoordinator::Lease writerLease;
                if (m_workCoordinator) {
                    writerLease = m_workCoordinator->acquire({
                        IndexingWorkCoordinator::Resource::SqliteWriter,
                        IndexingWorkCoordinator::Priority::Foreground,
                        false,
                        workGeneration});
                }
                if (m_workCoordinator && !writerLease) {
                    failJob(projectDatabasePath,
                            jobId,
                            QStringLiteral("缩略图状态写入因项目切换或应用退出而取消"));
                    return false;
                }
                if (!markThumbnailRunning(db, asset.id, &stateError)) {
                    Logger::warn(QStringLiteral(
                        "单文件缩略图状态写入失败，已跳过并继续：asset_id=%1 error=%2")
                                     .arg(asset.id)
                                     .arg(stateError));
                    ++failed;
                    ++processed;
                    continue;
                }
            }

            ThumbnailRequest request;
            request.assetId = asset.id;
            request.sourcePath = asset.absolutePath;
            request.cachePath = Paths::projectThumbnailCachePath(projectDatabasePath, sourceRootId, asset.id);
            request.assetType = asset.assetType;

            ThumbnailResult result;
            result.assetId = asset.id;
            if (!quota.withinHardLimit
                || trackedCacheBytes > quota.hardLimitBytes - EstimatedThumbnailBytes) {
                result.success = false;
                result.errorMessage = QStringLiteral(
                    "缩略图缓存已达到硬上限，当前文件已跳过；不会删除数据库或用户文件");
            } else {
                try {
                    result = m_thumbnailEngine->createPlaceholder(request);
                } catch (const std::exception &exception) {
                    result.assetId = asset.id;
                    result.success = false;
                    result.errorMessage = QStringLiteral("外部缩略图工具异常：%1")
                                              .arg(QString::fromUtf8(exception.what()));
                } catch (...) {
                    result.assetId = asset.id;
                    result.success = false;
                    result.errorMessage = QStringLiteral("外部缩略图工具发生未知异常");
                }
            }
            if (result.success) {
                const auto generatedBytes = qMax<qint64>(0, QFileInfo(result.outputPath).size());
                if (trackedCacheBytes > quota.hardLimitBytes - generatedBytes) {
                    QFile::remove(result.outputPath);
                    result.success = false;
                    result.outputPath.clear();
                    result.errorMessage = QStringLiteral(
                        "单文件生成后超过缩略图缓存硬上限，已仅回收该缓存文件");
                } else {
                    trackedCacheBytes += generatedBytes;
                }
            }
            QString persistError;
            IndexingWorkCoordinator::Lease writerLease;
            if (m_workCoordinator) {
                writerLease = m_workCoordinator->acquire({
                    IndexingWorkCoordinator::Resource::SqliteWriter,
                    IndexingWorkCoordinator::Priority::Foreground,
                    false,
                    workGeneration});
            }
            if (m_workCoordinator && !writerLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("缩略图写入因项目切换或应用退出而取消"));
                return false;
            }
            if (!persistThumbnail(db, result, &persistError)) {
                if (result.success && ThumbnailCacheQuota::isManagedThumbnailPath(result.outputPath)) {
                    QFile::remove(result.outputPath);
                }
                Logger::warn(QStringLiteral(
                    "单文件缩略图写入失败，已跳过并继续：asset_id=%1 error=%2")
                                 .arg(asset.id)
                                 .arg(persistError));
                ++failed;
            } else if (!result.success) {
                ++failed;
            }

            ++processed;
            const auto progress = progressFor(processed, total);
            if (progressGate.shouldPublish(processed, progress)) {
                updateJob(projectDatabasePath,
                          jobId,
                          progress,
                          QStringLiteral("已生成 %1/%2 张缩略图，失败 %3 张")
                              .arg(processed).arg(total).arg(failed),
                          itemProgressContext(
                              QStringLiteral("生成缩略图"), processed, total, QStringLiteral("张")));
            }
            if (!catalogPublishClock.isValid()
                || catalogPublishClock.elapsed() >= CatalogPublishIntervalMs) {
                notifyCatalogChanged(projectDatabasePath);
                catalogPublishClock.restart();
            }
        }
        auto finalQuota = ThumbnailCacheQuota::enforceForProject(projectDatabasePath);
        for (const auto &quotaError : std::as_const(finalQuota.errors)) {
            Logger::warn(quotaError);
        }
        quotaReferenceError.clear();
        if (!finalQuota.removedPaths.isEmpty() || finalQuota.requiresReferenceAudit) {
            if (!invalidateQuotaReferences(finalQuota, &quotaReferenceError)) {
                Logger::warn(QStringLiteral("更新已回收缩略图引用失败：%1")
                                 .arg(quotaReferenceError));
            } else {
                ThumbnailCacheQuota::completeReferenceAuditForProject(projectDatabasePath);
            }
        }
        telemetry.setQueueDepth(QStringLiteral("media.thumbnail_assets"), 0);
    }

    const auto finalProgress = progressFor(processed, total);
    if (progressGate.shouldPublish(processed, finalProgress, true)) {
        updateJob(projectDatabasePath,
                  jobId,
                  finalProgress,
                  QStringLiteral("已生成 %1/%2 张缩略图，失败 %3 张")
                      .arg(processed).arg(total).arg(failed),
                  itemProgressContext(
                      QStringLiteral("生成缩略图"), processed, total, QStringLiteral("张")));
    }

    if (processed > 0 && failed == processed) {
        failJob(projectDatabasePath, jobId, QStringLiteral("缩略图任务失败：%1 个文件均未成功").arg(failed));
        return false;
    }

    completeJob(projectDatabasePath, jobId, failed > 0
        ? QStringLiteral("缩略图生成完成，成功 %1 张，失败 %2 张").arg(processed - failed).arg(failed)
        : QStringLiteral("缩略图生成完成，共 %1 张").arg(processed));
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

bool MediaTaskService::invalidateEvictedThumbnails(
    QSqlDatabase &db,
    const ThumbnailCacheQuotaResult &quotaResult,
    QString *errorMessage) const
{
    const auto invalidateIds = [&](const QVector<qint64> &assetIds) {
        if (assetIds.isEmpty()) {
            return true;
        }
        if (!db.transaction()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
            return false;
        }
        QSqlQuery update(db);
        update.prepare(QStringLiteral(
            "UPDATE thumbnail SET status = ?, image_path = '', error_message = ?, updated_at = ? "
            "WHERE asset_id = ?"));
        const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
        for (const auto assetId : assetIds) {
            update.addBindValue(static_cast<int>(ThumbnailStatus::Pending));
            update.addBindValue(QStringLiteral("缩略图缓存已按 LRU 配额回收，等待按需重建"));
            update.addBindValue(now);
            update.addBindValue(assetId);
            if (!update.exec()) {
                const auto message = update.lastError().text();
                db.rollback();
                if (errorMessage) {
                    *errorMessage = message;
                }
                return false;
            }
            update.finish();
        }
        if (!db.commit()) {
            const auto message = db.lastError().text();
            db.rollback();
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }
        return true;
    };

    if (!quotaResult.removedPaths.isEmpty()) {
        QVector<qint64> assetIds;
        QSqlQuery lookup(db);
        lookup.prepare(QStringLiteral("SELECT asset_id FROM thumbnail WHERE image_path = ?"));
        for (const auto &removedPath : quotaResult.removedPaths) {
            lookup.addBindValue(removedPath);
            if (!lookup.exec()) {
                if (errorMessage) {
                    *errorMessage = lookup.lastError().text();
                }
                return false;
            }
            while (lookup.next()) {
                assetIds.append(lookup.value(0).toLongLong());
            }
            lookup.finish();
        }
        if (!invalidateIds(assetIds)) {
            return false;
        }
    }

    if (!quotaResult.requiresReferenceAudit) {
        return true;
    }
    qint64 lastAssetId = 0;
    while (true) {
        QSqlQuery audit(db);
        audit.prepare(QStringLiteral(
            "SELECT asset_id, image_path FROM thumbnail "
            "WHERE asset_id > ? AND status = ? AND image_path <> '' "
            "ORDER BY asset_id LIMIT 500"));
        audit.addBindValue(lastAssetId);
        audit.addBindValue(static_cast<int>(ThumbnailStatus::Success));
        if (!audit.exec()) {
            if (errorMessage) {
                *errorMessage = audit.lastError().text();
            }
            return false;
        }
        QVector<qint64> missingAssetIds;
        int rowCount = 0;
        while (audit.next()) {
            ++rowCount;
            lastAssetId = audit.value(0).toLongLong();
            const auto imagePath = audit.value(1).toString();
            if (ThumbnailCacheQuota::isManagedThumbnailPath(imagePath)
                && !QFileInfo::exists(imagePath)) {
                missingAssetIds.append(lastAssetId);
            }
        }
        audit.finish();
        if (!invalidateIds(missingAssetIds)) {
            return false;
        }
        if (rowCount < 500) {
            break;
        }
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
        if (projectDatabasePath.trimmed().isEmpty()) {
            return;
        }
        m_pendingCatalogPaths.insert(projectDatabasePath);
        if (!m_catalogChangeTimer.isActive()) {
            m_catalogChangeTimer.start();
        }
    }, Qt::QueuedConnection);
}

void MediaTaskService::flushCatalogChanged(const QString &projectDatabasePath)
{
    QMetaObject::invokeMethod(this, [this, projectDatabasePath]() {
        if (projectDatabasePath.trimmed().isEmpty()) {
            return;
        }
        m_pendingCatalogPaths.remove(projectDatabasePath);
        if (m_pendingCatalogPaths.isEmpty()) {
            m_catalogChangeTimer.stop();
        }
        emit mediaCatalogChanged(projectDatabasePath);
    }, Qt::QueuedConnection);
}

void MediaTaskService::publishPendingCatalogChanges()
{
    const auto projectDatabasePaths = m_pendingCatalogPaths.values();
    m_pendingCatalogPaths.clear();
    for (const auto &projectDatabasePath : projectDatabasePaths) {
        emit mediaCatalogChanged(projectDatabasePath);
    }
}
