#include "core/scan/ScanEngine.h"

#include "core/jobs/JobEngine.h"
#include "core/media/MediaProbeEngine.h"
#include "core/scan/FileTypeService.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "shared/FolderPathMetadata.h"
#include "shared/ScopedBackgroundThreadPriority.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {
bool canOpenFileForRead(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly);
}

QString toIsoString(const std::filesystem::file_time_type &fileTime)
{
    using namespace std::chrono;
    const auto systemNow = system_clock::now();
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    const auto translated = time_point_cast<system_clock::duration>(fileTime - fileNow + systemNow);
    return QDateTime::fromSecsSinceEpoch(duration_cast<seconds>(translated.time_since_epoch()).count()).toString(Qt::ISODate);
}

void bindSourceStats(QSqlQuery &query,
                     const ScanBatch &batch,
                     const QString &status,
                     qint64 videoCount,
                     qint64 audioCount,
                     qint64 imageCount,
                     qint64 otherCount,
                     int scanVersion)
{
    query.addBindValue(status);
    query.addBindValue(batch.totalFiles);
    query.addBindValue(batch.totalFolders);
    query.addBindValue(batch.totalSizeBytes);
    query.addBindValue(videoCount);
    query.addBindValue(audioCount);
    query.addBindValue(imageCount);
    query.addBindValue(otherCount);
    query.addBindValue(batch.warningCount);
    if (scanVersion >= 0) {
        query.addBindValue(scanVersion);
    }
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(batch.sourceRootId);
}

JobProgressContext scanProgressContext(const ScanBatch &batch)
{
    JobProgressContext context;
    context.currentStep = 1;
    context.totalSteps = 1;
    context.stepLabel = QStringLiteral("扫描目录");
    context.currentItem = batch.totalFiles;
    context.unitLabel = QStringLiteral("个文件");
    context.extraLabel = QStringLiteral("%1个文件夹").arg(batch.totalFolders);
    return context;
}

FolderNode makeFolderNode(const SourceRoot &sourceRoot,
                          const QString &rootFolderName,
                          const QString &absolutePath,
                          const QString &relativePath)
{
    FolderNode folder;
    folder.sourceRootId = sourceRoot.id;
    folder.name = FolderPathMetadata::folderName(absolutePath, sourceRoot.name);
    folder.absolutePath = absolutePath;
    folder.pathKey = FolderPathMetadata::normalizedPathKey(absolutePath);
    folder.relativePath = FolderPathMetadata::normalizeRelativePath(relativePath);
    folder.parentRelativePath = FolderPathMetadata::parentRelativePath(folder.relativePath);
    folder.depth = FolderPathMetadata::depth(folder.relativePath);
    const auto date = FolderPathMetadata::inferDate(rootFolderName, folder.relativePath);
    folder.normalizedDate = date.normalizedDate;
    folder.dateAnchor = date.anchorRelativePath;
    return folder;
}
}

ScanEngine::ScanEngine(DatabaseManager *databaseManager, JobEngine *jobEngine, MediaProbeEngine *mediaProbeEngine, ThumbnailEngine *thumbnailEngine, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_jobEngine(jobEngine)
    , m_mediaProbeEngine(mediaProbeEngine)
    , m_thumbnailEngine(thumbnailEngine)
{
}

void ScanEngine::startScan(const SourceRoot &sourceRoot, qint64 jobId)
{
    const auto projectDatabasePath = m_databaseManager
        ? QFileInfo(m_databaseManager->databaseFilePath()).absoluteFilePath()
        : QString();
    if (projectDatabasePath.isEmpty()) {
        const auto message = QStringLiteral("没有可用于扫描的项目数据库");
        if (m_jobEngine) {
            m_jobEngine->failJob(jobId, message);
        }
        emit scanFailed(sourceRoot.id, message);
        emit scanFailedForProject(projectDatabasePath, sourceRoot.id, message);
        return;
    }

    QString sessionError;
    const auto sessionId = prepareSession(sourceRoot, &sessionError);
    if (sessionId <= 0) {
        const auto message = sessionError.isEmpty()
            ? QStringLiteral("创建可恢复扫描会话失败")
            : sessionError;
        if (m_jobEngine) {
            m_jobEngine->failJob(jobId, message);
        }
        emit scanFailed(sourceRoot.id, message);
        emit scanFailedForProject(projectDatabasePath, sourceRoot.id, message);
        return;
    }

    const auto activeScanKey = QStringLiteral("%1|%2")
                                   .arg(FolderPathMetadata::normalizedPathKey(projectDatabasePath))
                                   .arg(sourceRoot.id);
    {
        QMutexLocker locker(&m_activeScansMutex);
        if (m_activeScans.contains(activeScanKey)) {
            const auto message = QStringLiteral("该素材源已有扫描任务正在运行");
            if (m_jobEngine) {
                m_jobEngine->failJob(jobId, message);
            }
            emit scanFailed(sourceRoot.id, message);
            emit scanFailedForProject(projectDatabasePath, sourceRoot.id, message);
            return;
        }
        m_activeScans.insert(activeScanKey);
    }

    auto future = QtConcurrent::run([this, sourceRoot, jobId, projectDatabasePath, activeScanKey, sessionId]() {
        runScan(sourceRoot, jobId, projectDatabasePath, activeScanKey, sessionId);
    });
    m_scanFutures.addFuture(future);
}

qint64 ScanEngine::prepareSession(const SourceRoot &sourceRoot, QString *errorMessage)
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目数据库未打开");
        }
        return 0;
    }

    auto db = m_databaseManager->database();
    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSqlQuery cleanup(db);
    cleanup.prepare(QStringLiteral("DELETE FROM scan_session WHERE source_root_id = ? AND state = 'completed'"));
    cleanup.addBindValue(sourceRoot.id);
    if (!cleanup.exec()) {
        if (errorMessage) {
            *errorMessage = cleanup.lastError().text();
        }
        return 0;
    }

    QSqlQuery existing(db);
    existing.prepare(QStringLiteral("SELECT id FROM scan_session WHERE source_root_id = ? LIMIT 1"));
    existing.addBindValue(sourceRoot.id);
    if (!existing.exec()) {
        if (errorMessage) {
            *errorMessage = existing.lastError().text();
        }
        return 0;
    }
    const auto obsoleteSessionId = existing.next() ? existing.value(0).toLongLong() : qint64{0};

    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return 0;
    }
    const auto rollback = [&db]() {
        db.rollback();
    };

    if (obsoleteSessionId > 0) {
        QSqlQuery resetGeneration(db);
        resetGeneration.prepare(QStringLiteral("DELETE FROM scan_session WHERE id = ?"));
        resetGeneration.addBindValue(obsoleteSessionId);
        if (!resetGeneration.exec()) {
            if (errorMessage) {
                *errorMessage = resetGeneration.lastError().text();
            }
            rollback();
            return 0;
        }
    }

    QSqlQuery createSession(db);
    createSession.prepare(QStringLiteral(
        "INSERT INTO scan_session (source_root_id, state, last_error, created_at, updated_at) "
        "VALUES (?, 'running', '', ?, ?)"));
    createSession.addBindValue(sourceRoot.id);
    createSession.addBindValue(now);
    createSession.addBindValue(now);
    if (!createSession.exec()) {
        if (errorMessage) {
            *errorMessage = createSession.lastError().text();
        }
        rollback();
        return 0;
    }
    const auto sessionId = createSession.lastInsertId().toLongLong();
    const auto rootFolderName = FolderPathMetadata::folderName(sourceRoot.path, sourceRoot.name);
    const auto rootFolder = makeFolderNode(sourceRoot, rootFolderName, sourceRoot.path, QString());

    QSqlQuery stageRoot(db);
    stageRoot.prepare(QStringLiteral(
        "INSERT INTO scan_stage_folder "
        "(session_id, path_key, name, absolute_path, relative_path, parent_relative_path, depth, "
        "file_count, direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0, 0, ?, ?, ?, ?)"));
    stageRoot.addBindValue(sessionId);
    stageRoot.addBindValue(rootFolder.pathKey);
    stageRoot.addBindValue(rootFolder.name);
    stageRoot.addBindValue(rootFolder.absolutePath);
    stageRoot.addBindValue(rootFolder.relativePath);
    stageRoot.addBindValue(rootFolder.parentRelativePath);
    stageRoot.addBindValue(rootFolder.depth);
    stageRoot.addBindValue(rootFolder.normalizedDate);
    stageRoot.addBindValue(rootFolder.dateAnchor);
    stageRoot.addBindValue(now);
    stageRoot.addBindValue(now);
    if (!stageRoot.exec()) {
        if (errorMessage) {
            *errorMessage = stageRoot.lastError().text();
        }
        rollback();
        return 0;
    }

    QSqlQuery rootWork(db);
    rootWork.prepare(QStringLiteral(
        "INSERT INTO scan_work_item "
        "(session_id, absolute_path, relative_path, path_key, depth, state, created_at, updated_at) "
        "VALUES (?, ?, '', ?, 0, 'pending', ?, ?)"));
    rootWork.addBindValue(sessionId);
    rootWork.addBindValue(sourceRoot.path);
    rootWork.addBindValue(rootFolder.pathKey);
    rootWork.addBindValue(now);
    rootWork.addBindValue(now);
    if (!rootWork.exec()) {
        if (errorMessage) {
            *errorMessage = rootWork.lastError().text();
        }
        rollback();
        return 0;
    }
    if (!db.commit()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        rollback();
        return 0;
    }
    return sessionId;
}

void ScanEngine::waitForIdle()
{
    m_scanFutures.waitForFinished();
}

void ScanEngine::setFailureAfterEntriesForTesting(qint64 entryCount)
{
    m_failureAfterEntries.store(entryCount);
}

void ScanEngine::releaseActiveScan(const QString &activeScanKey)
{
    QMutexLocker locker(&m_activeScansMutex);
    m_activeScans.remove(activeScanKey);
}

void ScanEngine::runScan(SourceRoot sourceRoot,
                         qint64 jobId,
                         const QString &projectDatabasePath,
                         const QString &activeScanKey,
                         qint64 sessionId)
{
    const ScopedBackgroundThreadPriority backgroundPriority;
    const auto activeScanGuard = qScopeGuard([this, activeScanKey]() {
        releaseActiveScan(activeScanKey);
    });
    const auto connectionName = QStringLiteral("scan_%1_%2").arg(sourceRoot.id).arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString errorMessage;
    auto db = m_databaseManager->openThreadConnectionForPath(projectDatabasePath,
                                                              connectionName,
                                                              &errorMessage);
    if (!db.isOpen()) {
        QMetaObject::invokeMethod(this, [this, sourceRoot, projectDatabasePath, errorMessage, jobId]() {
            if (m_jobEngine) {
                m_jobEngine->failJobForProject(projectDatabasePath, jobId, errorMessage);
            }
            if (m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath)) {
                emit scanFailed(sourceRoot.id, errorMessage);
            }
            emit scanFailedForProject(projectDatabasePath, sourceRoot.id, errorMessage);
        }, Qt::QueuedConnection);
        db = QSqlDatabase();
        m_databaseManager->closeThreadConnection(connectionName);
        return;
    }

    if (sessionId > 0) {
        runResumableScan(sourceRoot, jobId, projectDatabasePath, sessionId, db);
        db.close();
        db = QSqlDatabase();
        m_databaseManager->closeThreadConnection(connectionName);
        return;
    }

    ScanBatch batch;
    batch.sourceRootId = sourceRoot.id;
    qint64 videoCount = 0;
    qint64 audioCount = 0;
    qint64 imageCount = 0;
    qint64 otherCount = 0;

    auto invokeProgress = [this, projectDatabasePath, jobId](const ScanBatch &progressBatch) {
        QMetaObject::invokeMethod(this, [this, projectDatabasePath, progressBatch, jobId]() {
            if (m_jobEngine) {
                m_jobEngine->updateJobForProject(
                    projectDatabasePath,
                    jobId,
                    progressBatch.progressPercent,
                    QStringLiteral("已扫描 %1 个文件夹，%2 个文件")
                        .arg(progressBatch.totalFolders)
                        .arg(progressBatch.totalFiles),
                    scanProgressContext(progressBatch));
            }
            if (m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath)) {
                emit scanBatchCommitted(progressBatch);
            }
        }, Qt::QueuedConnection);
    };

    auto setupStageTables = [&]() -> bool {
        const QStringList statements = {
            QStringLiteral("DROP TABLE IF EXISTS temp.scan_asset_stage"),
            QStringLiteral("DROP TABLE IF EXISTS temp.scan_folder_stage"),
            QStringLiteral(
                "CREATE TEMP TABLE scan_asset_stage ("
                "source_root_id INTEGER NOT NULL, path_key TEXT NOT NULL PRIMARY KEY, name TEXT NOT NULL, "
                "extension TEXT, absolute_path TEXT NOT NULL, relative_path TEXT NOT NULL, parent_path TEXT NOT NULL, "
                "asset_type INTEGER NOT NULL, size_bytes INTEGER NOT NULL, modified_at TEXT NOT NULL, "
                "is_readable INTEGER NOT NULL, created_at TEXT NOT NULL)"),
            QStringLiteral(
                "CREATE TEMP TABLE scan_folder_stage ("
                "source_root_id INTEGER NOT NULL, path_key TEXT NOT NULL PRIMARY KEY, name TEXT NOT NULL, "
                "absolute_path TEXT NOT NULL, relative_path TEXT NOT NULL, parent_relative_path TEXT NOT NULL, "
                "depth INTEGER NOT NULL, file_count INTEGER NOT NULL, direct_file_count INTEGER NOT NULL, "
                "recursive_file_count INTEGER NOT NULL, normalized_date TEXT NOT NULL, date_anchor TEXT NOT NULL, "
                "created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
            QStringLiteral("CREATE INDEX temp.idx_scan_asset_source ON scan_asset_stage(source_root_id)"),
            QStringLiteral("CREATE INDEX temp.idx_scan_folder_source ON scan_folder_stage(source_root_id)")
        };
        QSqlQuery query(db);
        for (const auto &statement : statements) {
            if (!query.exec(statement)) {
                errorMessage = query.lastError().text();
                return false;
            }
        }
        return true;
    };

    auto stageFiles = [&](const QList<AssetFile> &files, qint64 progressPercent) -> bool {
        if (files.isEmpty()) {
            return true;
        }
        if (!db.transaction()) {
            errorMessage = db.lastError().text();
            return false;
        }
        QSqlQuery assetQuery(db);
        assetQuery.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO temp.scan_asset_stage "
            "(source_root_id, path_key, name, extension, absolute_path, relative_path, parent_path, asset_type, "
            "size_bytes, modified_at, is_readable, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

        for (const auto &file : files) {
            assetQuery.addBindValue(file.sourceRootId);
            assetQuery.addBindValue(FolderPathMetadata::normalizedPathKey(file.absolutePath));
            assetQuery.addBindValue(file.name);
            assetQuery.addBindValue(file.extension);
            assetQuery.addBindValue(file.absolutePath);
            assetQuery.addBindValue(file.relativePath);
            assetQuery.addBindValue(file.parentPath);
            assetQuery.addBindValue(static_cast<int>(file.assetType));
            assetQuery.addBindValue(file.sizeBytes);
            assetQuery.addBindValue(file.modifiedAt);
            assetQuery.addBindValue(file.readable ? 1 : 0);
            assetQuery.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
            if (!assetQuery.exec()) {
                errorMessage = assetQuery.lastError().text();
                db.rollback();
                return false;
            }
            assetQuery.finish();
        }
        if (!db.commit()) {
            errorMessage = db.lastError().text();
            db.rollback();
            return false;
        }
        batch.progressPercent = progressPercent;
        invokeProgress(batch);
        return true;
    };

    auto stageFolders = [&](const QList<FolderNode> &folders) -> bool {
        if (!db.transaction()) {
            errorMessage = db.lastError().text();
            return false;
        }
        QSqlQuery folderQuery(db);
        folderQuery.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO temp.scan_folder_stage "
            "(source_root_id, path_key, name, absolute_path, relative_path, parent_relative_path, depth, file_count, "
            "direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
        for (const auto &folder : folders) {
            folderQuery.addBindValue(folder.sourceRootId);
            folderQuery.addBindValue(folder.pathKey);
            folderQuery.addBindValue(folder.name);
            folderQuery.addBindValue(folder.absolutePath);
            folderQuery.addBindValue(folder.relativePath);
            folderQuery.addBindValue(folder.parentRelativePath);
            folderQuery.addBindValue(folder.depth);
            folderQuery.addBindValue(folder.fileCount);
            folderQuery.addBindValue(folder.directFileCount);
            folderQuery.addBindValue(folder.recursiveFileCount);
            folderQuery.addBindValue(folder.normalizedDate);
            folderQuery.addBindValue(folder.dateAnchor);
            folderQuery.addBindValue(now);
            folderQuery.addBindValue(now);
            if (!folderQuery.exec()) {
                errorMessage = folderQuery.lastError().text();
                db.rollback();
                return false;
            }
            folderQuery.finish();
        }
        if (!db.commit()) {
            errorMessage = db.lastError().text();
            db.rollback();
            return false;
        }
        return true;
    };

    auto executeForSource = [&](const QString &statement) -> bool {
        QSqlQuery query(db);
        query.prepare(statement);
        query.addBindValue(sourceRoot.id);
        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }
        return true;
    };

    auto reconcileStagedScan = [&]() -> bool {
        if (!db.transaction()) {
            errorMessage = db.lastError().text();
            return false;
        }
        auto rollback = [&]() {
            db.rollback();
            return false;
        };

        const QStringList statements = {
            QStringLiteral(
                "DELETE FROM embedded_metadata WHERE asset_id IN ("
                "SELECT af.id FROM asset_file af JOIN temp.scan_asset_stage s "
                "ON s.source_root_id = af.source_root_id AND s.path_key = af.path_key "
                "WHERE af.source_root_id = ? AND (af.size_bytes <> s.size_bytes OR af.modified_at <> s.modified_at))"),
            QStringLiteral(
                "DELETE FROM media_metadata WHERE asset_id IN ("
                "SELECT af.id FROM asset_file af JOIN temp.scan_asset_stage s "
                "ON s.source_root_id = af.source_root_id AND s.path_key = af.path_key "
                "WHERE af.source_root_id = ? AND (af.size_bytes <> s.size_bytes OR af.modified_at <> s.modified_at))"),
            QStringLiteral(
                "DELETE FROM thumbnail WHERE asset_id IN ("
                "SELECT af.id FROM asset_file af JOIN temp.scan_asset_stage s "
                "ON s.source_root_id = af.source_root_id AND s.path_key = af.path_key "
                "WHERE af.source_root_id = ? AND (af.size_bytes <> s.size_bytes OR af.modified_at <> s.modified_at))"),
            QStringLiteral(
                "UPDATE asset_file SET "
                "name = (SELECT s.name FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "extension = (SELECT s.extension FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "absolute_path = (SELECT s.absolute_path FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "relative_path = (SELECT s.relative_path FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "parent_path = (SELECT s.parent_path FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "asset_type = (SELECT s.asset_type FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "size_bytes = (SELECT s.size_bytes FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "modified_at = (SELECT s.modified_at FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key), "
                "is_readable = (SELECT s.is_readable FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key) "
                "WHERE source_root_id = ? AND EXISTS (SELECT 1 FROM temp.scan_asset_stage s "
                "WHERE s.source_root_id = asset_file.source_root_id AND s.path_key = asset_file.path_key)"),
            QStringLiteral(
                "INSERT INTO asset_file "
                "(source_root_id, name, extension, absolute_path, relative_path, parent_path, path_key, asset_type, "
                "size_bytes, modified_at, is_readable, created_at) "
                "SELECT s.source_root_id, s.name, s.extension, s.absolute_path, s.relative_path, s.parent_path, s.path_key, "
                "s.asset_type, s.size_bytes, s.modified_at, s.is_readable, s.created_at FROM temp.scan_asset_stage s "
                "WHERE s.source_root_id = ? AND NOT EXISTS (SELECT 1 FROM asset_file af "
                "WHERE af.source_root_id = s.source_root_id AND af.path_key = s.path_key)"),
            QStringLiteral(
                "DELETE FROM asset_file WHERE source_root_id = ? AND NOT EXISTS ("
                "SELECT 1 FROM temp.scan_asset_stage s WHERE s.source_root_id = asset_file.source_root_id "
                "AND s.path_key = asset_file.path_key)"),
            QStringLiteral(
                "UPDATE folder_node SET "
                "name = (SELECT s.name FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "absolute_path = (SELECT s.absolute_path FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "relative_path = (SELECT s.relative_path FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "parent_relative_path = (SELECT s.parent_relative_path FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "depth = (SELECT s.depth FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "file_count = (SELECT s.file_count FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "direct_file_count = (SELECT s.direct_file_count FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "recursive_file_count = (SELECT s.recursive_file_count FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "normalized_date = (SELECT s.normalized_date FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "date_anchor = (SELECT s.date_anchor FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key), "
                "updated_at = (SELECT s.updated_at FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key) "
                "WHERE source_root_id = ? AND EXISTS (SELECT 1 FROM temp.scan_folder_stage s "
                "WHERE s.source_root_id = folder_node.source_root_id AND s.path_key = folder_node.path_key)"),
            QStringLiteral(
                "INSERT INTO folder_node "
                "(source_root_id, name, absolute_path, path_key, relative_path, parent_relative_path, depth, file_count, "
                "direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
                "SELECT s.source_root_id, s.name, s.absolute_path, s.path_key, s.relative_path, s.parent_relative_path, "
                "s.depth, s.file_count, s.direct_file_count, s.recursive_file_count, s.normalized_date, s.date_anchor, "
                "s.created_at, s.updated_at FROM temp.scan_folder_stage s WHERE s.source_root_id = ? "
                "AND NOT EXISTS (SELECT 1 FROM folder_node fn WHERE fn.source_root_id = s.source_root_id AND fn.path_key = s.path_key)"),
            QStringLiteral(
                "DELETE FROM folder_node WHERE source_root_id = ? AND NOT EXISTS ("
                "SELECT 1 FROM temp.scan_folder_stage s WHERE s.source_root_id = folder_node.source_root_id "
                "AND s.path_key = folder_node.path_key)")
        };
        for (const auto &statement : statements) {
            if (!executeForSource(statement)) {
                return rollback();
            }
        }

        QSqlQuery sourceUpdate(db);
        sourceUpdate.prepare(QStringLiteral(
            "UPDATE source_root SET status = ?, total_files = ?, total_folders = ?, total_size_bytes = ?, "
            "video_count = ?, audio_count = ?, image_count = ?, other_count = ?, warning_count = ?, "
            "scan_version = ?, updated_at = ? WHERE id = ?"));
        bindSourceStats(sourceUpdate,
                        batch,
                        batch.warningCount > 0 ? QStringLiteral("warning") : QStringLiteral("ok"),
                        videoCount,
                        audioCount,
                        imageCount,
                        otherCount,
                        ScanEngine::CurrentScanVersion);
        if (!sourceUpdate.exec()) {
            errorMessage = sourceUpdate.lastError().text();
            return rollback();
        }
        if (!db.commit()) {
            errorMessage = db.lastError().text();
            return rollback();
        }
        return true;
    };

    QList<FolderNode> folders;
    QHash<QString, int> folderIndexes;
    QList<AssetFile> files;
    constexpr qint64 kBatchSize = 500;

    try {
        if (!setupStageTables()) {
            throw std::runtime_error(errorMessage.toStdString());
        }
        const auto rootFolderName = FolderPathMetadata::folderName(sourceRoot.path, sourceRoot.name);
        folders.append(makeFolderNode(sourceRoot, rootFolderName, sourceRoot.path, QStringLiteral("")));
        folderIndexes.insert(QStringLiteral(""), 0);

        const std::filesystem::path rootPath = sourceRoot.path.toStdWString();
        std::error_code iteratorError;
        std::filesystem::recursive_directory_iterator it(
            rootPath,
            std::filesystem::directory_options::skip_permission_denied,
            iteratorError);
        const std::filesystem::recursive_directory_iterator end;
        if (iteratorError) {
            throw std::runtime_error(iteratorError.message());
        }
        while (it != end) {
            const auto path = it->path();
            const auto absolutePath = QString::fromStdWString(path.wstring());
            const auto relativePath = FolderPathMetadata::relativePathFromRoot(sourceRoot.path, absolutePath);
            ++batch.processedEntries;

            std::error_code typeError;
            const auto isDirectory = it->is_directory(typeError);
            if (typeError) {
                ++batch.warningCount;
                typeError.clear();
            } else if (isDirectory) {
                const auto normalizedRelativePath = FolderPathMetadata::normalizeRelativePath(relativePath);
                const auto key = normalizedRelativePath.toCaseFolded();
                if (!folderIndexes.contains(key)) {
                    folderIndexes.insert(key, folders.size());
                    folders.append(makeFolderNode(sourceRoot,
                                                  rootFolderName,
                                                  absolutePath,
                                                  normalizedRelativePath));
                }
                ++batch.totalFolders;
            } else if (it->is_regular_file(typeError) && !typeError) {
                AssetFile file;
                file.sourceRootId = sourceRoot.id;
                file.name = QFileInfo(absolutePath).fileName();
                file.extension = QFileInfo(absolutePath).suffix().toLower();
                file.absolutePath = absolutePath;
                file.relativePath = FolderPathMetadata::normalizeRelativePath(relativePath);
                file.parentPath = QFileInfo(absolutePath).absolutePath();
                file.assetType = FileTypeService::classify(file.name);
                std::error_code metadataError;
                file.sizeBytes = static_cast<qint64>(it->file_size(metadataError));
                if (metadataError) {
                    file.sizeBytes = 0;
                    ++batch.warningCount;
                    metadataError.clear();
                }
                const auto modifiedTime = it->last_write_time(metadataError);
                file.modifiedAt = metadataError ? QString() : toIsoString(modifiedTime);
                if (metadataError) {
                    ++batch.warningCount;
                }
                file.readable = QFileInfo(absolutePath).isReadable()
                    && canOpenFileForRead(absolutePath);
                files.append(file);

                const auto parentRelativePath = FolderPathMetadata::parentRelativePath(file.relativePath);
                const auto directIndex = folderIndexes.value(parentRelativePath.toCaseFolded(), -1);
                if (directIndex >= 0) {
                    ++folders[directIndex].directFileCount;
                    folders[directIndex].fileCount = folders[directIndex].directFileCount;
                }
                for (const auto &ancestor : FolderPathMetadata::ancestorRelativePaths(parentRelativePath)) {
                    const auto ancestorIndex = folderIndexes.value(ancestor.toCaseFolded(), -1);
                    if (ancestorIndex >= 0) {
                        ++folders[ancestorIndex].recursiveFileCount;
                    }
                }

                ++batch.totalFiles;
                batch.totalSizeBytes += file.sizeBytes;
                if (!file.readable) {
                    ++batch.warningCount;
                }

                switch (file.assetType) {
                case AssetType::Video: ++videoCount; break;
                case AssetType::Audio: ++audioCount; break;
                case AssetType::Image: ++imageCount; break;
                default: ++otherCount; break;
                }
            }

            if (files.size() >= kBatchSize) {
                const auto progress = std::min<qint64>(95, 5 + (batch.totalFiles / 20));
                if (!stageFiles(files, progress)) {
                    throw std::runtime_error(QStringLiteral("扫描暂存写入失败：%1").arg(errorMessage).toStdString());
                }
                files.clear();
            }

            const auto failureAfterEntries = m_failureAfterEntries.load();
            if (failureAfterEntries >= 0 && batch.processedEntries >= failureAfterEntries) {
                throw std::runtime_error("测试注入：扫描在原子切换前失败");
            }

            it.increment(iteratorError);
            if (iteratorError) {
                ++batch.warningCount;
                iteratorError.clear();
            }
        }

        if (!stageFiles(files, 96) || !stageFolders(folders)) {
            throw std::runtime_error(QStringLiteral("扫描暂存收尾失败：%1").arg(errorMessage).toStdString());
        }
        if (!reconcileStagedScan()) {
            throw std::runtime_error(QStringLiteral("扫描结果原子切换失败：%1").arg(errorMessage).toStdString());
        }

        QMetaObject::invokeMethod(this, [this, sourceRoot, projectDatabasePath, batch, jobId]() {
            const auto stillCurrent = m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath);
            if (m_jobEngine) {
                m_jobEngine->completeJobForProject(
                    projectDatabasePath,
                    jobId,
                    QStringLiteral("%1 扫描完成，发现 %2 个文件")
                        .arg(sourceRoot.name)
                        .arg(batch.totalFiles));
            }
            if (stillCurrent) {
                emit scanFinished(sourceRoot.id);
            }
            emit scanFinishedForProject(projectDatabasePath, sourceRoot.id);
        }, Qt::QueuedConnection);
    } catch (const std::exception &exception) {
        auto message = QString::fromUtf8(exception.what()).trimmed();
        if (message.isEmpty()) {
            message = QStringLiteral("扫描失败");
        }
        QSqlQuery failUpdate(db);
        failUpdate.prepare(QStringLiteral("UPDATE source_root SET status = ?, warning_count = ?, updated_at = ? WHERE id = ?"));
        failUpdate.addBindValue(QStringLiteral("failed"));
        failUpdate.addBindValue(batch.warningCount + 1);
        failUpdate.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        failUpdate.addBindValue(sourceRoot.id);
        failUpdate.exec();

        QMetaObject::invokeMethod(this, [this, sourceRoot, projectDatabasePath, message, jobId]() {
            const auto stillCurrent = m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath);
            if (m_jobEngine) {
                m_jobEngine->failJobForProject(projectDatabasePath, jobId, message);
            }
            if (stillCurrent) {
                emit scanFailed(sourceRoot.id, message);
            }
            emit scanFailedForProject(projectDatabasePath, sourceRoot.id, message);
        }, Qt::QueuedConnection);
    }

    db.close();
    db = QSqlDatabase();
    m_databaseManager->closeThreadConnection(connectionName);
}

void ScanEngine::runResumableScan(SourceRoot sourceRoot,
                                  qint64 jobId,
                                  const QString &projectDatabasePath,
                                  qint64 sessionId,
                                  QSqlDatabase &db)
{
    QString errorMessage;
    qint64 reportedProgress = 0;
    QElapsedTimer progressTimer;

    auto readBatch = [&]() {
        ScanBatch batch;
        batch.sourceRootId = sourceRoot.id;

        QSqlQuery assets(db);
        assets.prepare(QStringLiteral(
            "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0), "
            "COALESCE(SUM(CASE WHEN is_readable = 0 THEN 1 ELSE 0 END), 0) "
            "FROM scan_stage_asset WHERE session_id = ?"));
        assets.addBindValue(sessionId);
        if (!assets.exec() || !assets.next()) {
            errorMessage = assets.lastError().text();
            return batch;
        }
        batch.totalFiles = assets.value(0).toLongLong();
        batch.totalSizeBytes = assets.value(1).toLongLong();
        batch.warningCount = assets.value(2).toLongLong();

        QSqlQuery folders(db);
        folders.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM scan_stage_folder WHERE session_id = ? AND relative_path <> ''"));
        folders.addBindValue(sessionId);
        if (!folders.exec() || !folders.next()) {
            errorMessage = folders.lastError().text();
            return batch;
        }
        batch.totalFolders = folders.value(0).toLongLong();

        QSqlQuery work(db);
        work.prepare(QStringLiteral(
            "SELECT COALESCE(SUM(CASE WHEN state IN ('completed', 'skipped') THEN 1 ELSE 0 END), 0), "
            "COUNT(*), COALESCE(SUM(CASE WHEN state = 'skipped' THEN 1 ELSE 0 END), 0) "
            "FROM scan_work_item WHERE session_id = ?"));
        work.addBindValue(sessionId);
        if (!work.exec() || !work.next()) {
            errorMessage = work.lastError().text();
            return batch;
        }
        batch.processedEntries = work.value(0).toLongLong();
        const auto totalDirectories = work.value(1).toLongLong();
        batch.warningCount += work.value(2).toLongLong();
        const auto estimated = totalDirectories > 0
            ? (batch.processedEntries * 95 / totalDirectories)
            : 0;
        reportedProgress = qMax(reportedProgress, qMin<qint64>(95, estimated));
        batch.progressPercent = reportedProgress;
        return batch;
    };

    auto publishProgress = [&](bool force) {
        if (!force && progressTimer.isValid() && progressTimer.elapsed() < 250) {
            return;
        }
        const auto batch = readBatch();
        if (!errorMessage.isEmpty()) {
            return;
        }
        progressTimer.restart();
        QMetaObject::invokeMethod(this, [this, projectDatabasePath, batch, jobId]() {
            if (m_jobEngine) {
                m_jobEngine->updateJobForProject(
                    projectDatabasePath,
                    jobId,
                    batch.progressPercent,
                    QStringLiteral("已完成 %1 个目录检查点，暂存 %2 个文件")
                        .arg(batch.processedEntries)
                        .arg(batch.totalFiles),
                    scanProgressContext(batch));
            }
            if (m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath)) {
                emit scanBatchCommitted(batch);
            }
        }, Qt::QueuedConnection);
    };

    auto interrupt = [&](const QString &message) {
        db.rollback();
        const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
        QSqlQuery session(db);
        session.prepare(QStringLiteral(
            "UPDATE scan_session SET state = 'interrupted', last_error = ?, updated_at = ? WHERE id = ?"));
        session.addBindValue(message);
        session.addBindValue(now);
        session.addBindValue(sessionId);
        session.exec();

        QSqlQuery source(db);
        source.prepare(QStringLiteral(
            "UPDATE source_root SET status = 'warning', warning_count = warning_count + 1, updated_at = ? WHERE id = ?"));
        source.addBindValue(now);
        source.addBindValue(sourceRoot.id);
        source.exec();

        QMetaObject::invokeMethod(this, [this, sourceRoot, projectDatabasePath, message, jobId]() {
            const auto stillCurrent = m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath);
            if (m_jobEngine) {
                m_jobEngine->failJobForProject(projectDatabasePath, jobId, message);
            }
            if (stillCurrent) {
                emit scanFailed(sourceRoot.id, message);
            }
            emit scanFailedForProject(projectDatabasePath, sourceRoot.id, message);
        }, Qt::QueuedConnection);
    };

    struct WorkItem {
        qint64 id = 0;
        QString absolutePath;
        QString relativePath;
        int depth = 0;
    };

    try {
        QSqlQuery resumeRunning(db);
        resumeRunning.prepare(QStringLiteral(
            "UPDATE scan_work_item SET state = 'pending', updated_at = ? WHERE session_id = ? AND state = 'running'"));
        resumeRunning.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        resumeRunning.addBindValue(sessionId);
        if (!resumeRunning.exec()) {
            throw std::runtime_error(resumeRunning.lastError().text().toStdString());
        }

        publishProgress(true);
        qint64 processedEntries = 0;
        const auto rootFolderName = FolderPathMetadata::folderName(sourceRoot.path, sourceRoot.name);

        while (true) {
            QSqlQuery next(db);
            next.prepare(QStringLiteral(
                "SELECT id, absolute_path, relative_path, depth FROM scan_work_item "
                "WHERE session_id = ? AND state = 'pending' ORDER BY depth, id LIMIT 1"));
            next.addBindValue(sessionId);
            if (!next.exec()) {
                throw std::runtime_error(next.lastError().text().toStdString());
            }
            if (!next.next()) {
                break;
            }
            WorkItem item;
            item.id = next.value(0).toLongLong();
            item.absolutePath = next.value(1).toString();
            item.relativePath = next.value(2).toString();
            item.depth = next.value(3).toInt();
            next.finish();

            const QFileInfo directoryInfo(item.absolutePath);
            if (!directoryInfo.isDir() || !directoryInfo.isReadable()) {
                if (item.depth <= 0) {
                    throw std::runtime_error(QStringLiteral("扫描根目录不可访问：%1").arg(item.absolutePath).toStdString());
                }
                const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
                const auto warning = QStringLiteral("已跳过不可访问的子目录：%1").arg(item.absolutePath);
                if (!db.transaction()) {
                    throw std::runtime_error(db.lastError().text().toStdString());
                }
                QSqlQuery preserveFolders(db);
                preserveFolders.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO scan_stage_folder "
                    "(session_id, path_key, name, absolute_path, relative_path, parent_relative_path, depth, "
                    "file_count, direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
                    "SELECT ?, path_key, name, absolute_path, relative_path, parent_relative_path, depth, "
                    "file_count, direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at "
                    "FROM folder_node WHERE source_root_id = ? AND "
                    "(relative_path = ? OR substr(relative_path, 1, length(?) + 1) = ? || '/')"));
                preserveFolders.addBindValue(sessionId);
                preserveFolders.addBindValue(sourceRoot.id);
                preserveFolders.addBindValue(item.relativePath);
                preserveFolders.addBindValue(item.relativePath);
                preserveFolders.addBindValue(item.relativePath);
                if (!preserveFolders.exec()) {
                    const auto message = preserveFolders.lastError().text();
                    db.rollback();
                    throw std::runtime_error(message.toStdString());
                }
                QSqlQuery preserveAssets(db);
                preserveAssets.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO scan_stage_asset "
                    "(session_id, path_key, name, extension, absolute_path, relative_path, parent_path, parent_relative_path, "
                    "asset_type, size_bytes, modified_at, is_readable, created_at) "
                    "SELECT ?, path_key, name, extension, absolute_path, relative_path, parent_path, "
                    "CASE WHEN length(relative_path) > length(name) "
                    "THEN substr(relative_path, 1, length(relative_path) - length(name) - 1) ELSE '' END, "
                    "asset_type, size_bytes, modified_at, is_readable, created_at "
                    "FROM asset_file WHERE source_root_id = ? AND "
                    "(relative_path = ? OR substr(relative_path, 1, length(?) + 1) = ? || '/')"));
                preserveAssets.addBindValue(sessionId);
                preserveAssets.addBindValue(sourceRoot.id);
                preserveAssets.addBindValue(item.relativePath);
                preserveAssets.addBindValue(item.relativePath);
                preserveAssets.addBindValue(item.relativePath);
                if (!preserveAssets.exec()) {
                    const auto message = preserveAssets.lastError().text();
                    db.rollback();
                    throw std::runtime_error(message.toStdString());
                }
                QSqlQuery skipWork(db);
                skipWork.prepare(QStringLiteral(
                    "UPDATE scan_work_item SET state = 'skipped', updated_at = ? WHERE id = ?"));
                skipWork.addBindValue(now);
                skipWork.addBindValue(item.id);
                if (!skipWork.exec()) {
                    db.rollback();
                    throw std::runtime_error(skipWork.lastError().text().toStdString());
                }
                QSqlQuery recordWarning(db);
                recordWarning.prepare(QStringLiteral(
                    "UPDATE scan_session SET last_error = ?, updated_at = ? WHERE id = ?"));
                recordWarning.addBindValue(warning);
                recordWarning.addBindValue(now);
                recordWarning.addBindValue(sessionId);
                if (!recordWarning.exec()) {
                    db.rollback();
                    throw std::runtime_error(recordWarning.lastError().text().toStdString());
                }
                if (!db.commit()) {
                    const auto message = db.lastError().text();
                    db.rollback();
                    throw std::runtime_error(message.toStdString());
                }
                publishProgress(false);
                continue;
            }
            struct DirectoryEntry {
                QString absolutePath;
                QString relativePath;
                QFileInfo info;
                bool readable = false;
            };
            QVector<DirectoryEntry> directoryEntries;
            QDirIterator entryIterator(
                item.absolutePath,
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                QDirIterator::NoIteratorFlags);
            while (entryIterator.hasNext()) {
                DirectoryEntry entry;
                entry.absolutePath = entryIterator.next();
                entry.info = QFileInfo(entry.absolutePath);
                entry.relativePath = FolderPathMetadata::normalizeRelativePath(
                    FolderPathMetadata::relativePathFromRoot(sourceRoot.path, entry.absolutePath));
                entry.readable = entry.info.isFile()
                    && entry.info.isReadable()
                    && canOpenFileForRead(entry.absolutePath);
                directoryEntries.append(std::move(entry));
            }

            if (!db.transaction()) {
                throw std::runtime_error(db.lastError().text().toStdString());
            }
            const auto rollbackWork = [&db]() {
                db.rollback();
            };
            const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);

            QSqlQuery markRunning(db);
            markRunning.prepare(QStringLiteral("UPDATE scan_work_item SET state = 'running', updated_at = ? WHERE id = ?"));
            markRunning.addBindValue(now);
            markRunning.addBindValue(item.id);
            if (!markRunning.exec()) {
                rollbackWork();
                throw std::runtime_error(markRunning.lastError().text().toStdString());
            }

            QSqlQuery stageFolder(db);
            stageFolder.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO scan_stage_folder "
                "(session_id, path_key, name, absolute_path, relative_path, parent_relative_path, depth, "
                "file_count, direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0, 0, ?, ?, ?, ?)"));
            QSqlQuery enqueueDirectory(db);
            enqueueDirectory.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO scan_work_item "
                "(session_id, absolute_path, relative_path, path_key, depth, state, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, 'pending', ?, ?)"));
            QSqlQuery stageFile(db);
            stageFile.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO scan_stage_asset "
                "(session_id, path_key, name, extension, absolute_path, relative_path, parent_path, parent_relative_path, "
                "asset_type, size_bytes, modified_at, is_readable, created_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

            for (const auto &entry : std::as_const(directoryEntries)) {
                const auto &absolutePath = entry.absolutePath;
                const auto &relativePath = entry.relativePath;
                const auto &info = entry.info;
                if (info.isDir()) {
                    const auto folder = makeFolderNode(sourceRoot, rootFolderName, absolutePath, relativePath);
                    stageFolder.addBindValue(sessionId);
                    stageFolder.addBindValue(folder.pathKey);
                    stageFolder.addBindValue(folder.name);
                    stageFolder.addBindValue(folder.absolutePath);
                    stageFolder.addBindValue(folder.relativePath);
                    stageFolder.addBindValue(folder.parentRelativePath);
                    stageFolder.addBindValue(folder.depth);
                    stageFolder.addBindValue(folder.normalizedDate);
                    stageFolder.addBindValue(folder.dateAnchor);
                    stageFolder.addBindValue(now);
                    stageFolder.addBindValue(now);
                    if (!stageFolder.exec()) {
                        const auto message = stageFolder.lastError().text();
                        rollbackWork();
                        throw std::runtime_error(message.toStdString());
                    }
                    stageFolder.finish();

                    enqueueDirectory.addBindValue(sessionId);
                    enqueueDirectory.addBindValue(folder.absolutePath);
                    enqueueDirectory.addBindValue(folder.relativePath);
                    enqueueDirectory.addBindValue(folder.pathKey);
                    enqueueDirectory.addBindValue(folder.depth);
                    enqueueDirectory.addBindValue(now);
                    enqueueDirectory.addBindValue(now);
                    if (!enqueueDirectory.exec()) {
                        const auto message = enqueueDirectory.lastError().text();
                        rollbackWork();
                        throw std::runtime_error(message.toStdString());
                    }
                    enqueueDirectory.finish();
                } else if (info.isFile()) {
                    const auto parentRelativePath = FolderPathMetadata::parentRelativePath(relativePath);
                    stageFile.addBindValue(sessionId);
                    stageFile.addBindValue(FolderPathMetadata::normalizedPathKey(absolutePath));
                    stageFile.addBindValue(info.fileName());
                    stageFile.addBindValue(info.suffix().toLower());
                    stageFile.addBindValue(absolutePath);
                    stageFile.addBindValue(relativePath);
                    stageFile.addBindValue(info.absolutePath());
                    stageFile.addBindValue(parentRelativePath);
                    stageFile.addBindValue(static_cast<int>(FileTypeService::classify(info.fileName())));
                    stageFile.addBindValue(info.size());
                    stageFile.addBindValue(info.lastModified().toString(Qt::ISODate));
                    stageFile.addBindValue(entry.readable ? 1 : 0);
                    stageFile.addBindValue(now);
                    if (!stageFile.exec()) {
                        const auto message = stageFile.lastError().text();
                        rollbackWork();
                        throw std::runtime_error(message.toStdString());
                    }
                    stageFile.finish();
                }

                ++processedEntries;
                const auto failureAfterEntries = m_failureAfterEntries.load();
                if (failureAfterEntries >= 0 && processedEntries >= failureAfterEntries) {
                    rollbackWork();
                    throw std::runtime_error("测试注入：扫描在目录检查点提交前中断");
                }
            }

            QSqlQuery completeWork(db);
            completeWork.prepare(QStringLiteral("UPDATE scan_work_item SET state = 'completed', updated_at = ? WHERE id = ?"));
            completeWork.addBindValue(now);
            completeWork.addBindValue(item.id);
            if (!completeWork.exec()) {
                const auto message = completeWork.lastError().text();
                rollbackWork();
                throw std::runtime_error(message.toStdString());
            }
            QSqlQuery touchSession(db);
            touchSession.prepare(QStringLiteral("UPDATE scan_session SET state = 'running', updated_at = ? WHERE id = ?"));
            touchSession.addBindValue(now);
            touchSession.addBindValue(sessionId);
            if (!touchSession.exec()) {
                const auto message = touchSession.lastError().text();
                rollbackWork();
                throw std::runtime_error(message.toStdString());
            }
            if (!db.commit()) {
                const auto message = db.lastError().text();
                rollbackWork();
                throw std::runtime_error(message.toStdString());
            }
            publishProgress(false);
        }

        QSqlQuery maxDepthQuery(db);
        maxDepthQuery.prepare(QStringLiteral("SELECT COALESCE(MAX(depth), 0) FROM scan_stage_folder WHERE session_id = ?"));
        maxDepthQuery.addBindValue(sessionId);
        if (!maxDepthQuery.exec() || !maxDepthQuery.next()) {
            throw std::runtime_error(maxDepthQuery.lastError().text().toStdString());
        }
        const auto maxDepth = maxDepthQuery.value(0).toInt();
        maxDepthQuery.finish();

        if (!db.transaction()) {
            throw std::runtime_error(db.lastError().text().toStdString());
        }
        QSqlQuery directCounts(db);
        directCounts.prepare(QStringLiteral(
            "UPDATE scan_stage_folder SET "
            "direct_file_count = (SELECT COUNT(*) FROM scan_stage_asset a "
            "WHERE a.session_id = ? AND a.parent_relative_path = scan_stage_folder.relative_path), "
            "file_count = (SELECT COUNT(*) FROM scan_stage_asset a "
            "WHERE a.session_id = ? AND a.parent_relative_path = scan_stage_folder.relative_path), "
            "recursive_file_count = 0 WHERE session_id = ?"));
        directCounts.addBindValue(sessionId);
        directCounts.addBindValue(sessionId);
        directCounts.addBindValue(sessionId);
        if (!directCounts.exec()) {
            const auto message = directCounts.lastError().text();
            db.rollback();
            throw std::runtime_error(message.toStdString());
        }
        for (auto depth = maxDepth; depth >= 0; --depth) {
            QSqlQuery recursiveCounts(db);
            recursiveCounts.prepare(QStringLiteral(
                "UPDATE scan_stage_folder SET recursive_file_count = direct_file_count + COALESCE(("
                "SELECT SUM(child.recursive_file_count) FROM scan_stage_folder child "
                "WHERE child.session_id = scan_stage_folder.session_id "
                "AND child.parent_relative_path = scan_stage_folder.relative_path), 0) "
                "WHERE session_id = ? AND depth = ?"));
            recursiveCounts.addBindValue(sessionId);
            recursiveCounts.addBindValue(depth);
            if (!recursiveCounts.exec()) {
                const auto message = recursiveCounts.lastError().text();
                db.rollback();
                throw std::runtime_error(message.toStdString());
            }
        }
        if (!db.commit()) {
            const auto message = db.lastError().text();
            db.rollback();
            throw std::runtime_error(message.toStdString());
        }

        const auto batch = readBatch();
        if (!errorMessage.isEmpty()) {
            throw std::runtime_error(errorMessage.toStdString());
        }
        qint64 videoCount = 0;
        qint64 audioCount = 0;
        qint64 imageCount = 0;
        qint64 otherCount = 0;
        QSqlQuery typeCounts(db);
        typeCounts.prepare(QStringLiteral(
            "SELECT "
            "COALESCE(SUM(CASE WHEN asset_type = ? THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN asset_type = ? THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN asset_type = ? THEN 1 ELSE 0 END), 0), "
            "COALESCE(SUM(CASE WHEN asset_type NOT IN (?, ?, ?) THEN 1 ELSE 0 END), 0) "
            "FROM scan_stage_asset WHERE session_id = ?"));
        typeCounts.addBindValue(static_cast<int>(AssetType::Video));
        typeCounts.addBindValue(static_cast<int>(AssetType::Audio));
        typeCounts.addBindValue(static_cast<int>(AssetType::Image));
        typeCounts.addBindValue(static_cast<int>(AssetType::Video));
        typeCounts.addBindValue(static_cast<int>(AssetType::Audio));
        typeCounts.addBindValue(static_cast<int>(AssetType::Image));
        typeCounts.addBindValue(sessionId);
        if (!typeCounts.exec() || !typeCounts.next()) {
            throw std::runtime_error(typeCounts.lastError().text().toStdString());
        }
        videoCount = typeCounts.value(0).toLongLong();
        audioCount = typeCounts.value(1).toLongLong();
        imageCount = typeCounts.value(2).toLongLong();
        otherCount = typeCounts.value(3).toLongLong();
        typeCounts.finish();

        const auto sessionLiteral = QString::number(sessionId);
        if (!db.transaction()) {
            throw std::runtime_error(db.lastError().text().toStdString());
        }
        const auto reconcile = [&](const QString &statement) {
            QSqlQuery query(db);
            query.prepare(statement);
            for (qsizetype index = 0; index < statement.count(QLatin1Char('?')); ++index) {
                query.addBindValue(sourceRoot.id);
            }
            if (!query.exec()) {
                errorMessage = query.lastError().text();
                return false;
            }
            return true;
        };
        const QStringList reconcileStatements = {
            QStringLiteral("DELETE FROM embedded_metadata WHERE asset_id IN ("
                           "SELECT af.id FROM asset_file af JOIN scan_stage_asset s "
                           "ON s.path_key = af.path_key WHERE af.source_root_id = ? AND s.session_id = %1 "
                           "AND (af.size_bytes <> s.size_bytes OR af.modified_at <> s.modified_at))").arg(sessionLiteral),
            QStringLiteral("DELETE FROM media_metadata WHERE asset_id IN ("
                           "SELECT af.id FROM asset_file af JOIN scan_stage_asset s "
                           "ON s.path_key = af.path_key WHERE af.source_root_id = ? AND s.session_id = %1 "
                           "AND (af.size_bytes <> s.size_bytes OR af.modified_at <> s.modified_at))").arg(sessionLiteral),
            QStringLiteral("DELETE FROM thumbnail WHERE asset_id IN ("
                           "SELECT af.id FROM asset_file af JOIN scan_stage_asset s "
                           "ON s.path_key = af.path_key WHERE af.source_root_id = ? AND s.session_id = %1 "
                           "AND (af.size_bytes <> s.size_bytes OR af.modified_at <> s.modified_at))").arg(sessionLiteral),
            QStringLiteral("UPDATE asset_file SET "
                           "name = (SELECT s.name FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "extension = (SELECT s.extension FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "absolute_path = (SELECT s.absolute_path FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "relative_path = (SELECT s.relative_path FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "parent_path = (SELECT s.parent_path FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "asset_type = (SELECT s.asset_type FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "size_bytes = (SELECT s.size_bytes FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "modified_at = (SELECT s.modified_at FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key), "
                           "is_readable = (SELECT s.is_readable FROM scan_stage_asset s WHERE s.session_id = %1 AND s.path_key = asset_file.path_key) "
                           "WHERE source_root_id = ? AND EXISTS (SELECT 1 FROM scan_stage_asset s "
                           "WHERE s.session_id = %1 AND s.path_key = asset_file.path_key)").arg(sessionLiteral),
            QStringLiteral("INSERT INTO asset_file "
                           "(source_root_id, name, extension, absolute_path, relative_path, parent_path, path_key, asset_type, size_bytes, modified_at, is_readable, created_at) "
                           "SELECT ?, s.name, s.extension, s.absolute_path, s.relative_path, s.parent_path, s.path_key, s.asset_type, s.size_bytes, s.modified_at, s.is_readable, s.created_at "
                           "FROM scan_stage_asset s WHERE s.session_id = %1 AND NOT EXISTS (SELECT 1 FROM asset_file af "
                           "WHERE af.source_root_id = ? AND af.path_key = s.path_key)").arg(sessionLiteral),
            QStringLiteral("DELETE FROM asset_file WHERE source_root_id = ? AND NOT EXISTS (SELECT 1 FROM scan_stage_asset s "
                           "WHERE s.session_id = %1 AND s.path_key = asset_file.path_key)").arg(sessionLiteral),
            QStringLiteral("UPDATE folder_node SET "
                           "name = (SELECT s.name FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "absolute_path = (SELECT s.absolute_path FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "relative_path = (SELECT s.relative_path FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "parent_relative_path = (SELECT s.parent_relative_path FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "depth = (SELECT s.depth FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "file_count = (SELECT s.file_count FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "direct_file_count = (SELECT s.direct_file_count FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "recursive_file_count = (SELECT s.recursive_file_count FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "normalized_date = (SELECT s.normalized_date FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "date_anchor = (SELECT s.date_anchor FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key), "
                           "updated_at = (SELECT s.updated_at FROM scan_stage_folder s WHERE s.session_id = %1 AND s.path_key = folder_node.path_key) "
                           "WHERE source_root_id = ? AND EXISTS (SELECT 1 FROM scan_stage_folder s "
                           "WHERE s.session_id = %1 AND s.path_key = folder_node.path_key)").arg(sessionLiteral),
            QStringLiteral("INSERT INTO folder_node "
                           "(source_root_id, name, absolute_path, path_key, relative_path, parent_relative_path, depth, file_count, direct_file_count, recursive_file_count, normalized_date, date_anchor, created_at, updated_at) "
                           "SELECT ?, s.name, s.absolute_path, s.path_key, s.relative_path, s.parent_relative_path, s.depth, s.file_count, s.direct_file_count, s.recursive_file_count, s.normalized_date, s.date_anchor, s.created_at, s.updated_at "
                           "FROM scan_stage_folder s WHERE s.session_id = %1 AND NOT EXISTS (SELECT 1 FROM folder_node fn "
                           "WHERE fn.source_root_id = ? AND fn.path_key = s.path_key)").arg(sessionLiteral),
            QStringLiteral("DELETE FROM folder_node WHERE source_root_id = ? AND NOT EXISTS (SELECT 1 FROM scan_stage_folder s "
                           "WHERE s.session_id = %1 AND s.path_key = folder_node.path_key)").arg(sessionLiteral)
        };
        for (const auto &statement : reconcileStatements) {
            if (!reconcile(statement)) {
                db.rollback();
                throw std::runtime_error(errorMessage.toStdString());
            }
        }

        QSqlQuery sourceUpdate(db);
        sourceUpdate.prepare(QStringLiteral(
            "UPDATE source_root SET status = ?, total_files = ?, total_folders = ?, total_size_bytes = ?, "
            "video_count = ?, audio_count = ?, image_count = ?, other_count = ?, warning_count = ?, "
            "scan_version = ?, updated_at = ? WHERE id = ?"));
        bindSourceStats(sourceUpdate,
                        batch,
                        batch.warningCount > 0 ? QStringLiteral("warning") : QStringLiteral("ok"),
                        videoCount,
                        audioCount,
                        imageCount,
                        otherCount,
                        CurrentScanVersion);
        if (!sourceUpdate.exec()) {
            const auto message = sourceUpdate.lastError().text();
            db.rollback();
            throw std::runtime_error(message.toStdString());
        }
        QSqlQuery completeSession(db);
        completeSession.prepare(QStringLiteral(
            "UPDATE scan_session SET state = 'completed', last_error = '', updated_at = ? WHERE id = ?"));
        completeSession.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        completeSession.addBindValue(sessionId);
        if (!completeSession.exec()) {
            const auto message = completeSession.lastError().text();
            db.rollback();
            throw std::runtime_error(message.toStdString());
        }
        if (!db.commit()) {
            const auto message = db.lastError().text();
            db.rollback();
            throw std::runtime_error(message.toStdString());
        }

        QSqlQuery cleanup(db);
        cleanup.prepare(QStringLiteral("DELETE FROM scan_session WHERE id = ? AND state = 'completed'"));
        cleanup.addBindValue(sessionId);
        cleanup.exec();

        QMetaObject::invokeMethod(this, [this, sourceRoot, projectDatabasePath, batch, jobId]() {
            const auto stillCurrent = m_databaseManager
                && FolderPathMetadata::normalizedPathKey(m_databaseManager->databaseFilePath())
                    == FolderPathMetadata::normalizedPathKey(projectDatabasePath);
            if (m_jobEngine) {
                m_jobEngine->completeJobForProject(
                    projectDatabasePath,
                    jobId,
                    QStringLiteral("%1 扫描完成，发现 %2 个文件")
                        .arg(sourceRoot.name)
                        .arg(batch.totalFiles));
            }
            if (stillCurrent) {
                emit scanFinished(sourceRoot.id);
            }
            emit scanFinishedForProject(projectDatabasePath, sourceRoot.id);
        }, Qt::QueuedConnection);
    } catch (const std::exception &exception) {
        auto message = QString::fromUtf8(exception.what()).trimmed();
        if (message.isEmpty()) {
            message = QStringLiteral("扫描已中断，等待下次自动恢复");
        }
        interrupt(message);
    }
}
