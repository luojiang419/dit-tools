#include "application/MediaTaskService.h"

#include "application/IndexingWorkCoordinator.h"
#include "application/JobProgressHeartbeat.h"

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
#include <array>
#include <exception>

namespace {
constexpr qint64 ProgressMinPublishIntervalMs = 250;
constexpr qint64 ProgressMaxPublishIntervalMs = 500;
constexpr int CatalogPublishIntervalMs = 500;
constexpr qsizetype MaxItemsPerJob = 128;
constexpr int ThumbnailRetryPolicyVersion = 1;
constexpr int MaxThumbnailRetryCount = 3;
constexpr int MetadataRetryPolicyVersion = 1;
constexpr int MaxMetadataRetryCount = 3;
constexpr int WorkLeaseTimeoutMs = 30000;
constexpr int ImmediateContinuationDelayMs = 250;
constexpr std::array<int, MaxThumbnailRetryCount> ThumbnailRetryDelaysMs = {
    5000,
    30000,
    120000,
};
constexpr std::array<int, MaxMetadataRetryCount> MetadataRetryDelaysMs = {
    5000,
    30000,
    120000,
};

int thumbnailRetryDelayMs(int retryCount)
{
    const auto index = qBound(1, retryCount, MaxThumbnailRetryCount) - 1;
    return ThumbnailRetryDelaysMs.at(static_cast<size_t>(index));
}

int metadataRetryDelayMs(int retryCount)
{
    const auto index = qBound(1, retryCount, MaxMetadataRetryCount) - 1;
    return MetadataRetryDelaysMs.at(static_cast<size_t>(index));
}

QString pendingMetadataPredicate(const QString &metadataAlias)
{
    return QStringLiteral(
        "AND (%1.asset_id IS NULL OR (%1.probe_status = %2 "
        "AND COALESCE(%1.failure_kind, '') = 'transient' "
        "AND COALESCE(%1.retry_count, 0) <= %3 "
        "AND COALESCE(%1.next_retry_at, '') <> '' "
        "AND %1.next_retry_at <= ?)) ")
        .arg(metadataAlias)
        .arg(static_cast<int>(ProbeStatus::Failed))
        .arg(MaxMetadataRetryCount);
}

bool retryableMetadataError(const QString &message)
{
    const auto normalized = message.toCaseFolded();
    return normalized.contains(QStringLiteral("超时"))
        || normalized.contains(QStringLiteral("temporarily unavailable"))
        || normalized.contains(QStringLiteral("input/output"))
        || normalized.contains(QStringLiteral("i/o error"))
        || normalized.contains(QStringLiteral("sharing violation"))
        || normalized.contains(QStringLiteral("being used by another process"));
}

bool retryableMediaProbeFailure(const MediaProbeResult &result)
{
    return result.status == ProbeStatus::Failed
        && retryableMetadataError(result.errorMessage);
}

QString technicalMetadataProgressDetail(qint64 processed,
                                        qint64 total,
                                        qint64 failed,
                                        qint64 deferred)
{
    auto detail = QStringLiteral("已读取技术参数 %1/%2 个文件，失败 %3 个")
                      .arg(processed)
                      .arg(total)
                      .arg(failed);
    if (deferred > 0) {
        detail += QStringLiteral("，待重试 %1 个").arg(deferred);
    }
    return detail;
}

QString pendingThumbnailPredicate(const QString &thumbnailAlias)
{
    return QStringLiteral(
        "AND (%1.asset_id IS NULL OR %1.status IN (%2, %3) "
        "OR (%1.status = %4 AND COALESCE(%1.image_path, '') = '') "
        "OR (%1.status = %5 AND ("
        "(LOWER(COALESCE(af.extension, '')) IN ('cr2', 'cr3') "
        "AND COALESCE(%1.retry_policy_version, 0) < %6) "
        "OR (COALESCE(%1.failure_kind, '') = 'transient' "
        "AND COALESCE(%1.retry_count, 0) <= %7 "
        "AND COALESCE(%1.next_retry_at, '') <> '' "
        "AND %1.next_retry_at <= ?)))) ")
        .arg(thumbnailAlias)
        .arg(static_cast<int>(ThumbnailStatus::Pending))
        .arg(static_cast<int>(ThumbnailStatus::Running))
        .arg(static_cast<int>(ThumbnailStatus::Success))
        .arg(static_cast<int>(ThumbnailStatus::Failed))
        .arg(ThumbnailRetryPolicyVersion)
        .arg(MaxThumbnailRetryCount);
}

qint64 progressFor(qint64 processed, qint64 total)
{
    if (total <= 0) {
        return 100;
    }
    return qBound<qint64>(qint64{1}, (static_cast<qint64>(processed) * 100) / static_cast<qint64>(total), qint64{100});
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
    , m_jobHeartbeat(new JobProgressHeartbeat(jobEngine, this))
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

    const auto now = QDateTime::currentDateTime();
    const auto projectDatabasePath = m_databaseManager->databaseFilePath();
    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT sr.name, sr.path, "
        "(SELECT COUNT(*) FROM asset_file af LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        " WHERE af.source_root_id = sr.id AND af.is_readable = 1 AND af.asset_type IN (?, ?, ?) %1), "
        "(SELECT COUNT(*) FROM asset_file af LEFT JOIN thumbnail th ON th.asset_id = af.id "
        " WHERE af.source_root_id = sr.id AND af.is_readable = 1 AND af.asset_type IN (?, ?) %2) "
        "FROM source_root sr WHERE sr.id = ?")
                      .arg(pendingMetadataPredicate(QStringLiteral("mm")),
                           pendingThumbnailPredicate(QStringLiteral("th"))));
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Audio));
    query.addBindValue(static_cast<int>(AssetType::Image));
    query.addBindValue(now.toString(Qt::ISODate));
    query.addBindValue(static_cast<int>(AssetType::Video));
    query.addBindValue(static_cast<int>(AssetType::Image));
    query.addBindValue(now.toString(Qt::ISODate));
    query.addBindValue(sourceRootId);
    if (!query.exec()) {
        Logger::warn(QStringLiteral("读取媒体任务待办数量失败：source_root_id=%1 error=%2")
                         .arg(sourceRootId)
                         .arg(query.lastError().text()));
        return;
    }
    if (!query.next()) {
        Logger::warn(QStringLiteral("媒体任务素材源不存在：source_root_id=%1")
                         .arg(sourceRootId));
        return;
    }

    const auto sourceName = query.value(0).toString();
    const auto sourcePath = query.value(1).toString();
    const auto metadataBacklogCount = query.value(2).toLongLong();
    const auto thumbnailBacklogCount = query.value(3).toLongLong();
    const auto pendingMetadataCount = qMin<qint64>(metadataBacklogCount, MaxItemsPerJob);
    const auto pendingThumbnailCount = qMin<qint64>(thumbnailBacklogCount, MaxItemsPerJob);
    const bool needsMetadata = pendingMetadataCount > 0 && m_mediaProbeEngine;
    const bool needsThumbnails = pendingThumbnailCount > 0 && m_thumbnailEngine;

    if (!needsMetadata && !needsThumbnails) {
        qint64 nextDelayMs = -1;
        const auto rememberSoonestRetry = [&](const QDateTime &retryAt, int maxDelayMs) {
            if (retryAt.isValid()) {
                const auto delayMs = qBound<qint64>(
                    static_cast<qint64>(ImmediateContinuationDelayMs),
                    now.msecsTo(retryAt),
                    static_cast<qint64>(maxDelayMs));
                if (nextDelayMs < 0 || delayMs < nextDelayMs) {
                    nextDelayMs = delayMs;
                }
            }
        };
        if (m_mediaProbeEngine) {
            QSqlQuery nextMetadataRetry(m_databaseManager->database());
            nextMetadataRetry.prepare(QStringLiteral(
                "SELECT MIN(mm.next_retry_at) FROM media_metadata mm "
                "JOIN asset_file af ON af.id = mm.asset_id "
                "WHERE af.source_root_id = ? AND mm.probe_status = ? "
                "AND COALESCE(mm.failure_kind, '') = 'transient' "
                "AND COALESCE(mm.retry_count, 0) <= ? "
                "AND COALESCE(mm.next_retry_at, '') > ?"));
            nextMetadataRetry.addBindValue(sourceRootId);
            nextMetadataRetry.addBindValue(static_cast<int>(ProbeStatus::Failed));
            nextMetadataRetry.addBindValue(MaxMetadataRetryCount);
            nextMetadataRetry.addBindValue(now.toString(Qt::ISODate));
            if (nextMetadataRetry.exec() && nextMetadataRetry.next()) {
                rememberSoonestRetry(
                    QDateTime::fromString(nextMetadataRetry.value(0).toString(), Qt::ISODate),
                    MetadataRetryDelaysMs.back());
            }
        }
        if (m_thumbnailEngine) {
            QSqlQuery nextThumbnailRetry(m_databaseManager->database());
            nextThumbnailRetry.prepare(QStringLiteral(
                "SELECT MIN(th.next_retry_at) FROM thumbnail th "
                "JOIN asset_file af ON af.id = th.asset_id "
                "WHERE af.source_root_id = ? AND th.status = ? "
                "AND COALESCE(th.failure_kind, '') = 'transient' "
                "AND COALESCE(th.retry_count, 0) <= ? "
                "AND COALESCE(th.next_retry_at, '') > ?"));
            nextThumbnailRetry.addBindValue(sourceRootId);
            nextThumbnailRetry.addBindValue(static_cast<int>(ThumbnailStatus::Failed));
            nextThumbnailRetry.addBindValue(MaxThumbnailRetryCount);
            nextThumbnailRetry.addBindValue(now.toString(Qt::ISODate));
            if (nextThumbnailRetry.exec() && nextThumbnailRetry.next()) {
                rememberSoonestRetry(
                    QDateTime::fromString(nextThumbnailRetry.value(0).toString(), Qt::ISODate),
                    ThumbnailRetryDelaysMs.back());
            }
        }
        if (nextDelayMs >= 0) {
            scheduleSourceRootRetry(projectDatabasePath,
                                    sourceRootId,
                                    static_cast<int>(nextDelayMs));
        }
        return;
    }

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

    Logger::info(QStringLiteral(
        "media_task_dispatch source_root_id=%1 metadata_batch=%2 metadata_backlog=%3 "
        "thumbnail_batch=%4 thumbnail_backlog=%5")
                     .arg(sourceRootId)
                     .arg(pendingMetadataCount)
                     .arg(metadataBacklogCount)
                     .arg(pendingThumbnailCount)
                     .arg(thumbnailBacklogCount));

    qint64 metadataJobId = 0;
    qint64 thumbnailJobId = 0;
    if (needsMetadata) {
        metadataJobId = m_jobEngine->createJob(JobType::Metadata,
                                               QStringLiteral("读取技术元数据 %1").arg(sourceName),
                                               QStringLiteral("准备读取视频/音频/图片技术参数"),
                                               sourceRootId,
                                               sourceRootSubject(sourceRootId, sourceName, sourcePath),
                                               itemProgressContext(QStringLiteral("读取技术参数"), 0, pendingMetadataCount, QStringLiteral("个文件")));
    }
    if (needsThumbnails) {
        thumbnailJobId = m_jobEngine->createJob(JobType::Thumbnail,
                                                QStringLiteral("生成缩略图 %1").arg(sourceName),
                                                QStringLiteral("准备生成视频/图片缩略图"),
                                                sourceRootId,
                                                sourceRootSubject(sourceRootId, sourceName, sourcePath),
                                                itemProgressContext(QStringLiteral("生成缩略图"), 0, pendingThumbnailCount, QStringLiteral("张")));
    }

    QVector<JobType> retryTypes;
    if (metadataJobId > 0) {
        retryTypes.append(JobType::Metadata);
    }
    if (thumbnailJobId > 0) {
        retryTypes.append(JobType::Thumbnail);
    }
    m_jobEngine->clearFailedJobsForRetry(sourceRootId, retryTypes);

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
                        workGeneration,
                        WorkLeaseTimeoutMs});
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
        ? pendingMetadataPredicate(QStringLiteral("pending"))
        : pendingThumbnailPredicate(QStringLiteral("pending"));
    const auto ordering = pendingWork == PendingWork::Metadata
        ? QStringLiteral(
              "ORDER BY CASE WHEN pending.probe_status = %1 THEN 1 ELSE 0 END, af.id ")
              .arg(static_cast<int>(ProbeStatus::Failed))
        : QStringLiteral(
              "ORDER BY CASE WHEN pending.status = %1 THEN 1 ELSE 0 END, af.id ")
              .arg(static_cast<int>(ThumbnailStatus::Failed));
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, af.name, af.extension, af.absolute_path, af.relative_path, af.parent_path, "
        "af.asset_type, af.size_bytes, af.modified_at, af.is_readable FROM asset_file af %1"
        "WHERE af.source_root_id = ? AND af.id > ? AND af.is_readable = 1 "
        "AND af.asset_type IN (%2) %3%4LIMIT ?")
                      .arg(join,
                           placeholders.join(QStringLiteral(",")),
                           pendingPredicate,
                           ordering));
    query.addBindValue(sourceRootId);
    query.addBindValue(lastAssetId);
    for (const auto assetType : assetTypes) {
        query.addBindValue(static_cast<int>(assetType));
    }
    if (pendingWork == PendingWork::Metadata || pendingWork == PendingWork::Thumbnail) {
        query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
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
        scheduleSourceRootRetry(projectDatabasePath,
                                sourceRootId,
                                thumbnailRetryDelayMs(1));
        return;
    }

    // A usable preview is the first visible result after an import. Generate
    // thumbnails before the slower per-file metadata probe so cards populate
    // progressively instead of staying blank until the whole source is probed.
    bool shouldContinueImmediately = true;
    if (thumbnailJobId > 0) {
        shouldContinueImmediately = runThumbnailJob(
            db, projectDatabasePath, sourceRootId, thumbnailJobId, thumbnailTotal, workGeneration);
        flushCatalogChanged(projectDatabasePath);
    }

    if (metadataJobId > 0) {
        shouldContinueImmediately = runMetadataJob(
            db, projectDatabasePath, metadataJobId, sourceRootId, metadataTotal, workGeneration)
            && shouldContinueImmediately;
        flushCatalogChanged(projectDatabasePath);
    }

    closeConnection();
    if (shouldContinueImmediately) {
        scheduleSourceRootRetry(projectDatabasePath,
                                sourceRootId,
                                ImmediateContinuationDelayMs);
    }
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
        completeJob(projectDatabasePath, jobId, QStringLiteral("没有需要读取技术元数据的文件"));
        return true;
    }

    constexpr qsizetype PageSize = 128;
    auto &telemetry = PerformanceTelemetry::global();
    qint64 processed = 0;
    qint64 failed = 0;
    qint64 deferred = 0;
    qint64 lastAssetId = 0;
    ProgressPublishGate progressGate;
    while (processed < total) {
        QString fetchError;
        const auto assets = fetchAssets(
            db,
            sourceRootId,
            {AssetType::Video, AssetType::Audio, AssetType::Image},
            PendingWork::Metadata,
            lastAssetId,
            static_cast<qsizetype>(qMin<qint64>(PageSize, total - processed)),
            &fetchError);
        if (!fetchError.isEmpty()) {
            telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), 0);
            failJob(projectDatabasePath, jobId, fetchError);
            return false;
        }
        if (assets.isEmpty()) {
            break;
        }
        telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), assets.size());
        lastAssetId = assets.constLast().id;
        for (const auto &asset : assets) {
            const auto activeItem = qMin<qint64>(processed + 1, total);
            const auto activeProgress = activeProgressFor(processed, total);
            const auto activeContext = itemProgressContext(
                QStringLiteral("读取技术参数"), activeItem, total, QStringLiteral("个文件"));
            const auto activeDetail = QStringLiteral("正在读取技术参数 %1/%2 个文件：%3")
                                          .arg(activeItem)
                                          .arg(total)
                                          .arg(assetDisplayName(asset));
            IndexingWorkCoordinator::Lease heavyIoLease;
            if (m_workCoordinator) {
                startJobHeartbeat(projectDatabasePath,
                                  jobId,
                                  activeProgress,
                                  activeDetail,
                                  activeContext,
                                  QStringLiteral("等待执行资源"));
                const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                    stopJobHeartbeat(jobId);
                });
                heavyIoLease = m_workCoordinator->acquire({
                    IndexingWorkCoordinator::Resource::HeavyIo,
                    IndexingWorkCoordinator::Priority::Background,
                    false,
                    workGeneration,
                    WorkLeaseTimeoutMs});
                if (!heavyIoLease) {
                    telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), 0);
                    failJob(projectDatabasePath,
                            jobId,
                            QStringLiteral("元数据任务等待执行资源超时，已安排自动重试"));
                    scheduleSourceRootRetry(projectDatabasePath,
                                            sourceRootId,
                                            metadataRetryDelayMs(1));
                    return false;
                }
            }
            MediaProbeResult result;
            result.assetId = asset.id;
            try {
                startJobHeartbeat(projectDatabasePath,
                                  jobId,
                                  activeProgress,
                                  activeDetail,
                                  activeContext,
                                  QStringLiteral("ffprobe"));
                const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                    stopJobHeartbeat(jobId);
                });
                result = m_mediaProbeEngine->probe(asset);
            } catch (const std::exception &exception) {
                result.status = ProbeStatus::Failed;
                result.errorMessage = QStringLiteral("外部媒体探测异常：%1")
                                          .arg(QString::fromUtf8(exception.what()));
            } catch (...) {
                result.status = ProbeStatus::Failed;
                result.errorMessage = QStringLiteral("外部媒体探测发生未知异常");
            }
            heavyIoLease.reset();
            QString persistError;
            IndexingWorkCoordinator::Lease writerLease;
            if (m_workCoordinator) {
                startJobHeartbeat(projectDatabasePath,
                                  jobId,
                                  activeProgress,
                                  activeDetail,
                                  activeContext,
                                  QStringLiteral("等待写入资源"));
                const auto heartbeatGuard = qScopeGuard([this, jobId]() {
                    stopJobHeartbeat(jobId);
                });
                writerLease = m_workCoordinator->acquire({
                    IndexingWorkCoordinator::Resource::SqliteWriter,
                    IndexingWorkCoordinator::Priority::Background,
                    false,
                    workGeneration,
                    WorkLeaseTimeoutMs});
            }
            if (m_workCoordinator && !writerLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("元数据写入等待超时，已安排自动重试"));
                scheduleSourceRootRetry(projectDatabasePath,
                                        sourceRootId,
                                        metadataRetryDelayMs(1));
                return false;
            }
            bool retryScheduled = false;
            int retryDelayMs = 0;
            if (!persistMediaProbe(db,
                                   result,
                                   &retryScheduled,
                                   &retryDelayMs,
                                   &persistError)) {
                Logger::warn(QStringLiteral(
                    "单文件元数据写入失败，已延后重试：asset_id=%1 error=%2")
                                 .arg(asset.id)
                                 .arg(persistError));
                ++deferred;
                scheduleSourceRootRetry(projectDatabasePath,
                                        sourceRootId,
                                        metadataRetryDelayMs(1));
            } else if (retryScheduled) {
                ++deferred;
                scheduleSourceRootRetry(projectDatabasePath,
                                        sourceRootId,
                                        retryDelayMs);
            } else if (result.status != ProbeStatus::Success) {
                ++failed;
            }

            ++processed;
            const auto progress = progressFor(processed, total);
            if (progressGate.shouldPublish(processed, progress)) {
                updateJob(projectDatabasePath,
                          jobId,
                          progress,
                          technicalMetadataProgressDetail(processed, total, failed, deferred),
                          itemProgressContext(
                              QStringLiteral("读取技术参数"), processed, total, QStringLiteral("个文件")));
            }
        }
        telemetry.setQueueDepth(QStringLiteral("media.ffprobe_assets"), 0);
    }

    const auto finalProgress = progressFor(processed, total);
    if (progressGate.shouldPublish(processed, finalProgress, true)) {
        updateJob(projectDatabasePath,
                  jobId,
                  finalProgress,
                  technicalMetadataProgressDetail(processed, total, failed, deferred),
                  itemProgressContext(
                      QStringLiteral("读取技术参数"), processed, total, QStringLiteral("个文件")));
    }

    if (processed > 0 && failed == processed && deferred == 0) {
        failJob(projectDatabasePath, jobId, QStringLiteral("技术元数据任务失败：%1 个文件均未成功").arg(failed));
        return false;
    }

    const auto successful = qMax<qint64>(0, processed - failed - deferred);
    if (failed > 0 && deferred > 0) {
        completeJob(projectDatabasePath,
                    jobId,
                    QStringLiteral("技术元数据读取完成，成功 %1 个，失败 %2 个，待重试 %3 个")
                        .arg(successful)
                        .arg(failed)
                        .arg(deferred));
    } else if (deferred > 0) {
        completeJob(projectDatabasePath,
                    jobId,
                    QStringLiteral("技术元数据读取完成，成功 %1 个，待重试 %2 个")
                        .arg(successful)
                        .arg(deferred));
    } else {
        completeJob(projectDatabasePath, jobId, failed > 0
            ? QStringLiteral("技术元数据读取完成，成功 %1 个，失败 %2 个").arg(successful).arg(failed)
            : QStringLiteral("技术元数据读取完成，共 %1 个文件").arg(processed));
    }
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
    qint64 deferred = 0;
    qint64 lastAssetId = 0;
    ProgressPublishGate progressGate;
    QElapsedTimer catalogPublishClock;
    while (processed < total) {
        QString fetchError;
        const auto assets = fetchAssets(
            db,
            sourceRootId,
            {AssetType::Video, AssetType::Image},
            PendingWork::Thumbnail,
            lastAssetId,
            static_cast<qsizetype>(qMin<qint64>(PageSize, total - processed)),
            &fetchError);
        if (!fetchError.isEmpty()) {
            telemetry.setQueueDepth(QStringLiteral("media.thumbnail_assets"), 0);
            failJob(projectDatabasePath, jobId, fetchError);
            return false;
        }
        if (assets.isEmpty()) {
            break;
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
                    workGeneration,
                    WorkLeaseTimeoutMs});
            }
            if (m_workCoordinator && !writerLease) {
                if (quotaError) {
                    *quotaError = QStringLiteral("缩略图引用修复等待写入资源超时，留待下次恢复");
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
            QElapsedTimer assetWatchdog;
            assetWatchdog.start();
            QString stateError;
            {
                IndexingWorkCoordinator::Lease writerLease;
                if (m_workCoordinator) {
                    writerLease = m_workCoordinator->acquire({
                        IndexingWorkCoordinator::Resource::SqliteWriter,
                        IndexingWorkCoordinator::Priority::Foreground,
                        false,
                        workGeneration,
                        WorkLeaseTimeoutMs});
                }
                if (m_workCoordinator && !writerLease) {
                    failJob(projectDatabasePath,
                            jobId,
                            QStringLiteral("缩略图状态写入等待超时，已安排自动重试"));
                    scheduleSourceRootRetry(projectDatabasePath,
                                            sourceRootId,
                                            thumbnailRetryDelayMs(1));
                    return false;
                }
                if (!markThumbnailRunning(db, asset.id, &stateError)) {
                    Logger::warn(QStringLiteral(
                        "单文件缩略图状态写入失败，已延后重试：asset_id=%1 error=%2")
                                     .arg(asset.id)
                                     .arg(stateError));
                    ++deferred;
                    ++processed;
                    scheduleSourceRootRetry(projectDatabasePath,
                                            sourceRootId,
                                            thumbnailRetryDelayMs(1));
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
                IndexingWorkCoordinator::Lease heavyIoLease;
                if (m_workCoordinator) {
                    heavyIoLease = m_workCoordinator->acquire({
                        IndexingWorkCoordinator::Resource::HeavyIo,
                        IndexingWorkCoordinator::Priority::Foreground,
                        false,
                        workGeneration,
                        WorkLeaseTimeoutMs});
                }
                if (m_workCoordinator && !heavyIoLease) {
                    telemetry.setQueueDepth(QStringLiteral("media.thumbnail_assets"), 0);
                    failJob(projectDatabasePath,
                            jobId,
                            QStringLiteral("缩略图任务等待执行资源超时，已安排自动重试"));
                    scheduleSourceRootRetry(projectDatabasePath,
                                            sourceRootId,
                                            thumbnailRetryDelayMs(1));
                    return false;
                }
                try {
                    result = m_thumbnailEngine->createPlaceholder(request);
                } catch (const std::exception &exception) {
                    result.assetId = asset.id;
                    result.success = false;
                    result.retryable = true;
                    result.errorMessage = QStringLiteral("外部缩略图工具异常：%1")
                                              .arg(QString::fromUtf8(exception.what()));
                } catch (...) {
                    result.assetId = asset.id;
                    result.success = false;
                    result.retryable = true;
                    result.errorMessage = QStringLiteral("外部缩略图工具发生未知异常");
                }
                heavyIoLease.reset();
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
                    workGeneration,
                    WorkLeaseTimeoutMs});
            }
            if (m_workCoordinator && !writerLease) {
                failJob(projectDatabasePath,
                        jobId,
                        QStringLiteral("缩略图结果写入等待超时，已安排自动重试"));
                scheduleSourceRootRetry(projectDatabasePath,
                                        sourceRootId,
                                        thumbnailRetryDelayMs(1));
                return false;
            }
            bool retryScheduled = false;
            int retryDelayMs = 0;
            if (!persistThumbnail(db,
                                  result,
                                  &retryScheduled,
                                  &retryDelayMs,
                                  &persistError)) {
                if (result.success && ThumbnailCacheQuota::isManagedThumbnailPath(result.outputPath)) {
                    QFile::remove(result.outputPath);
                }
                Logger::warn(QStringLiteral(
                    "单文件缩略图写入失败，已跳过并继续：asset_id=%1 error=%2")
                                 .arg(asset.id)
                                 .arg(persistError));
                ++deferred;
                scheduleSourceRootRetry(projectDatabasePath,
                                        sourceRootId,
                                        thumbnailRetryDelayMs(1));
            } else if (retryScheduled) {
                ++deferred;
                scheduleSourceRootRetry(projectDatabasePath,
                                        sourceRootId,
                                        retryDelayMs);
            } else if (!result.success) {
                ++failed;
            }

            if (assetWatchdog.elapsed() >= 90000) {
                Logger::warn(QStringLiteral(
                    "thumbnail_watchdog asset_id=%1 elapsed_ms=%2 outcome=%3")
                                 .arg(asset.id)
                                 .arg(assetWatchdog.elapsed())
                                 .arg(retryScheduled ? QStringLiteral("retry_scheduled")
                                                     : QStringLiteral("completed")));
            }

            ++processed;
            const auto progress = progressFor(processed, total);
            if (progressGate.shouldPublish(processed, progress)) {
                updateJob(projectDatabasePath,
                          jobId,
                          progress,
                          QStringLiteral("已处理 %1/%2 张缩略图，失败 %3 张，待重试 %4 张")
                              .arg(processed).arg(total).arg(failed).arg(deferred),
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
                  QStringLiteral("已处理 %1/%2 张缩略图，失败 %3 张，待重试 %4 张")
                      .arg(processed).arg(total).arg(failed).arg(deferred),
                  itemProgressContext(
                      QStringLiteral("生成缩略图"), processed, total, QStringLiteral("张")));
    }

    if (processed > 0 && failed == processed && deferred == 0) {
        failJob(projectDatabasePath, jobId, QStringLiteral("缩略图任务失败：%1 个文件均未成功").arg(failed));
        return false;
    }

    const auto succeeded = qMax<qint64>(0, processed - failed - deferred);
    completeJob(projectDatabasePath,
                jobId,
                deferred > 0
                    ? QStringLiteral("本批处理完成，成功 %1 张，失败 %2 张，已安排重试 %3 张")
                          .arg(succeeded).arg(failed).arg(deferred)
                    : (failed > 0
                           ? QStringLiteral("缩略图生成完成，成功 %1 张，失败 %2 张")
                                 .arg(succeeded).arg(failed)
                           : QStringLiteral("缩略图生成完成，共 %1 张").arg(processed)));
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

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO thumbnail "
        "(asset_id, status, image_path, updated_at, error_message, retry_count, "
        "next_retry_at, retry_policy_version, failure_kind) "
        "VALUES (?, ?, '', ?, '', 0, '', ?, '')"));
    insert.addBindValue(assetId);
    insert.addBindValue(static_cast<int>(ThumbnailStatus::Pending));
    insert.addBindValue(now);
    insert.addBindValue(ThumbnailRetryPolicyVersion);
    if (!insert.exec()) {
        if (errorMessage) {
            *errorMessage = insert.lastError().text();
        }
        return false;
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral(
        "UPDATE thumbnail SET status = ?, image_path = '', updated_at = ?, "
        "error_message = '', next_retry_at = '', retry_policy_version = ?, failure_kind = '' "
        "WHERE asset_id = ?"));
    update.addBindValue(static_cast<int>(ThumbnailStatus::Running));
    update.addBindValue(now);
    update.addBindValue(ThumbnailRetryPolicyVersion);
    update.addBindValue(assetId);
    if (!update.exec()) {
        if (errorMessage) {
            *errorMessage = update.lastError().text();
        }
        return false;
    }
    return true;
}

bool MediaTaskService::persistMediaProbe(QSqlDatabase &db,
                                         const MediaProbeResult &result,
                                         bool *retryScheduled,
                                         int *retryDelayMs,
                                         QString *errorMessage) const
{
    if (retryScheduled) {
        *retryScheduled = false;
    }
    if (retryDelayMs) {
        *retryDelayMs = 0;
    }

    int currentRetryCount = 0;
    QSqlQuery current(db);
    current.prepare(QStringLiteral(
        "SELECT COALESCE(retry_count, 0) FROM media_metadata WHERE asset_id = ?"));
    current.addBindValue(result.assetId);
    if (!current.exec()) {
        if (errorMessage) {
            *errorMessage = current.lastError().text();
        }
        return false;
    }
    if (current.next()) {
        currentRetryCount = qMax(0, current.value(0).toInt());
    }

    const auto now = QDateTime::currentDateTime();
    const auto nowText = now.toString(Qt::ISODate);
    auto storedRetryCount = result.status == ProbeStatus::Success ? 0 : currentRetryCount;
    QString nextRetryAt = QStringLiteral("");
    QString failureKind = QStringLiteral("");
    if (retryableMediaProbeFailure(result)) {
        if (currentRetryCount < MaxMetadataRetryCount) {
            storedRetryCount = currentRetryCount + 1;
            const auto delayMs = metadataRetryDelayMs(storedRetryCount);
            nextRetryAt = now.addMSecs(delayMs).toString(Qt::ISODate);
            failureKind = QStringLiteral("transient");
            if (retryScheduled) {
                *retryScheduled = true;
            }
            if (retryDelayMs) {
                *retryDelayMs = delayMs;
            }
        } else {
            failureKind = QStringLiteral("retry_exhausted");
        }
    } else if (result.status != ProbeStatus::Success) {
        failureKind = QStringLiteral("permanent");
    }

    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    QSqlQuery metadata(db);
    metadata.prepare(QStringLiteral(
        "INSERT INTO media_metadata "
        "(asset_id, probe_status, media_type, container, duration_ms, bit_rate, raw_json, error_message, "
        "retry_count, next_retry_at, retry_policy_version, failure_kind, last_attempt_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(asset_id) DO UPDATE SET "
        "probe_status = excluded.probe_status, media_type = excluded.media_type, "
        "container = excluded.container, duration_ms = excluded.duration_ms, "
        "bit_rate = excluded.bit_rate, raw_json = excluded.raw_json, "
        "error_message = excluded.error_message, retry_count = excluded.retry_count, "
        "next_retry_at = excluded.next_retry_at, retry_policy_version = excluded.retry_policy_version, "
        "failure_kind = excluded.failure_kind, last_attempt_at = excluded.last_attempt_at, "
        "updated_at = excluded.updated_at"));
    metadata.addBindValue(result.assetId);
    metadata.addBindValue(static_cast<int>(result.status));
    metadata.addBindValue(static_cast<int>(result.mediaType));
    metadata.addBindValue(result.format.container);
    metadata.addBindValue(result.format.durationMs);
    metadata.addBindValue(result.format.bitRate);
    metadata.addBindValue(result.rawJson);
    metadata.addBindValue(result.errorMessage);
    metadata.addBindValue(storedRetryCount);
    metadata.addBindValue(nextRetryAt);
    metadata.addBindValue(MetadataRetryPolicyVersion);
    metadata.addBindValue(failureKind);
    metadata.addBindValue(nowText);
    metadata.addBindValue(nowText);
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

bool MediaTaskService::persistThumbnail(QSqlDatabase &db,
                                        const ThumbnailResult &result,
                                        bool *retryScheduled,
                                        int *retryDelayMs,
                                        QString *errorMessage) const
{
    if (retryScheduled) {
        *retryScheduled = false;
    }
    if (retryDelayMs) {
        *retryDelayMs = 0;
    }

    int currentRetryCount = 0;
    QSqlQuery current(db);
    current.prepare(QStringLiteral(
        "SELECT COALESCE(retry_count, 0) FROM thumbnail WHERE asset_id = ?"));
    current.addBindValue(result.assetId);
    if (!current.exec()) {
        if (errorMessage) {
            *errorMessage = current.lastError().text();
        }
        return false;
    }
    if (current.next()) {
        currentRetryCount = qMax(0, current.value(0).toInt());
    }

    const auto now = QDateTime::currentDateTime();
    auto status = result.success ? ThumbnailStatus::Success : ThumbnailStatus::Failed;
    auto storedRetryCount = result.success ? 0 : currentRetryCount;
    QString nextRetryAt = QStringLiteral("");
    QString failureKind = QStringLiteral("");
    if (!result.success && result.retryable) {
        if (currentRetryCount < MaxThumbnailRetryCount) {
            storedRetryCount = currentRetryCount + 1;
            const auto delayMs = thumbnailRetryDelayMs(storedRetryCount);
            nextRetryAt = now.addMSecs(delayMs).toString(Qt::ISODate);
            failureKind = QStringLiteral("transient");
            if (retryScheduled) {
                *retryScheduled = true;
            }
            if (retryDelayMs) {
                *retryDelayMs = delayMs;
            }
        } else {
            failureKind = QStringLiteral("retry_exhausted");
        }
    } else if (!result.success) {
        failureKind = QStringLiteral("permanent");
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO thumbnail "
        "(asset_id, status, image_path, updated_at, error_message, retry_count, "
        "next_retry_at, retry_policy_version, failure_kind) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(asset_id) DO UPDATE SET "
        "status = excluded.status, image_path = excluded.image_path, "
        "updated_at = excluded.updated_at, error_message = excluded.error_message, "
        "retry_count = excluded.retry_count, next_retry_at = excluded.next_retry_at, "
        "retry_policy_version = excluded.retry_policy_version, "
        "failure_kind = excluded.failure_kind"));
    query.addBindValue(result.assetId);
    query.addBindValue(static_cast<int>(status));
    query.addBindValue(result.success ? result.outputPath : QString());
    query.addBindValue(now.toString(Qt::ISODate));
    query.addBindValue(result.errorMessage);
    query.addBindValue(storedRetryCount);
    query.addBindValue(nextRetryAt);
    query.addBindValue(ThumbnailRetryPolicyVersion);
    query.addBindValue(failureKind);
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

void MediaTaskService::startJobHeartbeat(const QString &projectDatabasePath,
                                         qint64 jobId,
                                         qint64 progress,
                                         const QString &detailPrefix,
                                         const JobProgressContext &progressContext,
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
                               progressContext,
                               waitLabel]() {
        heartbeat->start(projectDatabasePath,
                         jobId,
                         progress,
                         detailPrefix,
                         progressContext,
                         waitLabel);
    },
                              Qt::QueuedConnection);
}

void MediaTaskService::stopJobHeartbeat(qint64 jobId)
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

void MediaTaskService::completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    stopJobHeartbeat(jobId);
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, detail]() {
        engine->completeJobForProject(projectDatabasePath, jobId, detail);
    }, Qt::QueuedConnection);
}

void MediaTaskService::failJob(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    stopJobHeartbeat(jobId);
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, errorMessage]() {
        engine->failJobForProject(projectDatabasePath, jobId, errorMessage);
    }, Qt::QueuedConnection);
}

void MediaTaskService::releaseActiveKey(const QString &activeKey)
{
    QMutexLocker locker(&m_activeKeysMutex);
    m_activeKeys.remove(activeKey);
}

void MediaTaskService::scheduleSourceRootRetry(const QString &projectDatabasePath,
                                               qint64 sourceRootId,
                                               int delayMs)
{
    if (projectDatabasePath.trimmed().isEmpty() || sourceRootId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(this,
                              [this, projectDatabasePath, sourceRootId, delayMs]() {
        const auto retryKey = QStringLiteral("%1|%2")
                                  .arg(FolderPathMetadata::normalizedPathKey(
                                      projectDatabasePath))
                                  .arg(sourceRootId);
        auto *timer = m_retryTimers.value(retryKey, nullptr);
        const auto normalizedDelayMs = qMax(0, delayMs);
        if (timer && timer->isActive()
            && timer->remainingTime() <= normalizedDelayMs) {
            return;
        }
        if (!timer) {
            timer = new QTimer(this);
            timer->setSingleShot(true);
            m_retryTimers.insert(retryKey, timer);
            connect(timer, &QTimer::timeout, this,
                    [this, projectDatabasePath, sourceRootId, retryKey, timer]() {
                m_retryTimers.remove(retryKey);
                timer->deleteLater();
                if (!m_databaseManager || !m_databaseManager->hasOpenProject()
                    || FolderPathMetadata::normalizedPathKey(
                           m_databaseManager->databaseFilePath())
                        != FolderPathMetadata::normalizedPathKey(projectDatabasePath)) {
                    return;
                }
                startForSourceRoot(sourceRootId);
            });
        }
        timer->start(normalizedDelayMs);
    },
                              Qt::QueuedConnection);
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
