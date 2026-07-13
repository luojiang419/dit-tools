#include "core/scan/ScanEngine.h"

#include "core/jobs/JobEngine.h"
#include "core/media/MediaProbeEngine.h"
#include "core/scan/FileTypeService.h"
#include "core/thumbnail/ThumbnailEngine.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <filesystem>

namespace {
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
    auto future = QtConcurrent::run([this, sourceRoot, jobId]() {
        runScan(sourceRoot, jobId);
    });
    m_scanFutures.addFuture(future);
}

void ScanEngine::waitForIdle()
{
    m_scanFutures.waitForFinished();
}

void ScanEngine::runScan(SourceRoot sourceRoot, qint64 jobId)
{
    const auto connectionName = QStringLiteral("scan_%1_%2").arg(sourceRoot.id).arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString errorMessage;
    auto db = m_databaseManager->openThreadConnection(connectionName, &errorMessage);
    if (!db.isOpen()) {
        QMetaObject::invokeMethod(this, [this, sourceRoot, errorMessage, jobId]() {
            m_jobEngine->failJob(jobId, errorMessage);
            emit scanFailed(sourceRoot.id, errorMessage);
        }, Qt::QueuedConnection);
        return;
    }

    ScanBatch batch;
    batch.sourceRootId = sourceRoot.id;
    qint64 videoCount = 0;
    qint64 audioCount = 0;
    qint64 imageCount = 0;
    qint64 otherCount = 0;

    QSqlQuery clearFolders(db);
    clearFolders.prepare(QStringLiteral("DELETE FROM folder_node WHERE source_root_id = ?"));
    clearFolders.addBindValue(sourceRoot.id);
    QSqlQuery clearAssets(db);
    clearAssets.prepare(QStringLiteral("DELETE FROM asset_file WHERE source_root_id = ?"));
    clearAssets.addBindValue(sourceRoot.id);
    if (!clearFolders.exec() || !clearAssets.exec()) {
        const auto message = clearFolders.lastError().isValid()
            ? clearFolders.lastError().text()
            : clearAssets.lastError().text();
        QMetaObject::invokeMethod(this, [this, sourceRoot, message, jobId]() {
            m_jobEngine->failJob(jobId, message);
            emit scanFailed(sourceRoot.id, message);
        }, Qt::QueuedConnection);
        m_databaseManager->closeThreadConnection(connectionName);
        return;
    }

    auto flush = [&](const QList<FolderNode> &folders, const QList<AssetFile> &files, qint64 progressPercent) -> bool {
        if (!db.transaction()) {
            return false;
        }

        QSqlQuery folderQuery(db);
        folderQuery.prepare(QStringLiteral(
            "INSERT INTO folder_node (source_root_id, absolute_path, relative_path, file_count, created_at) "
            "VALUES (?, ?, ?, ?, ?)"));

        for (const auto &folder : folders) {
            folderQuery.addBindValue(folder.sourceRootId);
            folderQuery.addBindValue(folder.absolutePath);
            folderQuery.addBindValue(folder.relativePath);
            folderQuery.addBindValue(folder.fileCount);
            folderQuery.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
            if (!folderQuery.exec()) {
                db.rollback();
                return false;
            }
        }

        QSqlQuery assetQuery(db);
        assetQuery.prepare(QStringLiteral(
            "INSERT INTO asset_file (source_root_id, name, extension, absolute_path, relative_path, parent_path, asset_type, size_bytes, modified_at, is_readable, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

        for (const auto &file : files) {
            assetQuery.addBindValue(file.sourceRootId);
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
                db.rollback();
                return false;
            }
        }

        QSqlQuery sourceUpdate(db);
        sourceUpdate.prepare(QStringLiteral(
            "UPDATE source_root SET status = ?, total_files = ?, total_folders = ?, total_size_bytes = ?, "
            "video_count = ?, audio_count = ?, image_count = ?, other_count = ?, warning_count = ?, updated_at = ? WHERE id = ?"));
        batch.progressPercent = progressPercent;
        bindSourceStats(sourceUpdate, batch, QStringLiteral("scanning"), videoCount, audioCount, imageCount, otherCount, -1);
        if (!sourceUpdate.exec()) {
            db.rollback();
            return false;
        }

        if (!db.commit()) {
            db.rollback();
            return false;
        }

        QMetaObject::invokeMethod(this, [this, batch, jobId]() {
            m_jobEngine->updateJob(jobId,
                                   batch.progressPercent,
                                   QStringLiteral("已扫描 %1 个文件夹，%2 个文件").arg(batch.totalFolders).arg(batch.totalFiles),
                                   scanProgressContext(batch));
            emit scanBatchCommitted(batch);
        }, Qt::QueuedConnection);

        return true;
    };

    QList<FolderNode> folders;
    QList<AssetFile> files;
    constexpr qint64 kBatchSize = 500;

    try {
        const std::filesystem::path rootPath = sourceRoot.path.toStdWString();
        for (std::filesystem::recursive_directory_iterator it(rootPath, std::filesystem::directory_options::skip_permission_denied), end; it != end; ++it) {
            const auto path = it->path();
            const auto absolutePath = QString::fromStdWString(path.wstring());
            const auto relativePath = QDir(sourceRoot.path).relativeFilePath(absolutePath);
            ++batch.processedEntries;

            if (it->is_directory()) {
                FolderNode folder;
                folder.sourceRootId = sourceRoot.id;
                folder.absolutePath = absolutePath;
                folder.relativePath = relativePath;
                folders.append(folder);
                ++batch.totalFolders;
            } else if (it->is_regular_file()) {
                AssetFile file;
                file.sourceRootId = sourceRoot.id;
                file.name = QFileInfo(absolutePath).fileName();
                file.extension = QFileInfo(absolutePath).suffix().toLower();
                file.absolutePath = absolutePath;
                file.relativePath = relativePath;
                file.parentPath = QFileInfo(absolutePath).absolutePath();
                file.assetType = FileTypeService::classify(file.name);
                file.sizeBytes = static_cast<qint64>(it->file_size());
                file.modifiedAt = toIsoString(it->last_write_time());
                file.readable = QFileInfo(absolutePath).isReadable();
                files.append(file);

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

            if ((folders.size() + files.size()) >= kBatchSize) {
                const auto progress = std::min<qint64>(95, 5 + (batch.totalFiles / 20));
                if (!flush(folders, files, progress)) {
                    throw std::runtime_error("数据库批量写入失败");
                }
                folders.clear();
                files.clear();
            }
        }

        if (!folders.isEmpty() || !files.isEmpty()) {
            if (!flush(folders, files, 98)) {
                throw std::runtime_error("数据库收尾写入失败");
            }
        }

        QSqlQuery finalUpdate(db);
        finalUpdate.prepare(QStringLiteral(
            "UPDATE source_root SET status = ?, total_files = ?, total_folders = ?, total_size_bytes = ?, "
            "video_count = ?, audio_count = ?, image_count = ?, other_count = ?, warning_count = ?, scan_version = ?, updated_at = ? WHERE id = ?"));
        bindSourceStats(finalUpdate,
                        batch,
                        batch.warningCount > 0 ? QStringLiteral("warning") : QStringLiteral("ok"),
                        videoCount,
                        audioCount,
                        imageCount,
                        otherCount,
                        ScanEngine::CurrentScanVersion);
        finalUpdate.exec();

        QMetaObject::invokeMethod(this, [this, sourceRoot, batch, jobId]() {
            m_jobEngine->completeJob(jobId, QStringLiteral("%1 扫描完成，发现 %2 个文件").arg(sourceRoot.name).arg(batch.totalFiles));
            emit scanFinished(sourceRoot.id);
        }, Qt::QueuedConnection);
    } catch (const std::exception &exception) {
        const auto message = QString::fromUtf8(exception.what());
        QSqlQuery failUpdate(db);
        failUpdate.prepare(QStringLiteral("UPDATE source_root SET status = ?, warning_count = ?, updated_at = ? WHERE id = ?"));
        failUpdate.addBindValue(QStringLiteral("failed"));
        failUpdate.addBindValue(batch.warningCount + 1);
        failUpdate.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        failUpdate.addBindValue(sourceRoot.id);
        failUpdate.exec();

        QMetaObject::invokeMethod(this, [this, sourceRoot, message, jobId]() {
            m_jobEngine->failJob(jobId, message);
            emit scanFailed(sourceRoot.id, message);
        }, Qt::QueuedConnection);
    }

    m_databaseManager->closeThreadConnection(connectionName);
}
