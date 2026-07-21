#include "application/ImportService.h"

#include "application/JobService.h"
#include "core/jobs/JobEngine.h"
#include "core/scan/ScanEngine.h"
#include "core/scan/ScanPathPolicy.h"
#include "infrastructure/db/DatabaseManager.h"
#include "shared/FolderPathMetadata.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

namespace {
SourceRoot readSourceRoot(const QSqlQuery &query)
{
    SourceRoot sourceRoot;
    sourceRoot.id = query.value(0).toLongLong();
    sourceRoot.name = query.value(1).toString();
    sourceRoot.path = query.value(2).toString();
    sourceRoot.status = query.value(3).toString();
    sourceRoot.totalFiles = query.value(4).toLongLong();
    sourceRoot.totalFolders = query.value(5).toLongLong();
    sourceRoot.totalSizeBytes = query.value(6).toLongLong();
    sourceRoot.videoCount = query.value(7).toLongLong();
    sourceRoot.audioCount = query.value(8).toLongLong();
    sourceRoot.imageCount = query.value(9).toLongLong();
    sourceRoot.otherCount = query.value(10).toLongLong();
    sourceRoot.warningCount = query.value(11).toLongLong();
    sourceRoot.scanVersion = query.value(12).toInt();
    return sourceRoot;
}

JobSubject sourceRootJobSubject(const SourceRoot &sourceRoot)
{
    JobSubject subject;
    subject.kind = QStringLiteral("sourceRoot");
    subject.key = QString::number(sourceRoot.id);
    subject.name = sourceRoot.name;
    subject.path = sourceRoot.path;
    subject.typeLabel = QStringLiteral("素材源");
    return subject;
}

JobProgressContext scanProgressContext()
{
    JobProgressContext context;
    context.currentStep = 1;
    context.totalSteps = 1;
    context.stepLabel = QStringLiteral("扫描目录");
    context.unitLabel = QStringLiteral("个文件");
    return context;
}
}

ImportService::ImportService(DatabaseManager *databaseManager, JobService *jobService, ScanEngine *scanEngine, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_jobService(jobService)
    , m_scanEngine(scanEngine)
{
    connect(m_scanEngine, &ScanEngine::scanFinished, this, [this](qint64 sourceRootId) {
        emit catalogChanged();
        emit sourceScanFinished(sourceRootId);
    });
    connect(m_scanEngine, &ScanEngine::scanFailed, this, [this](qint64 sourceRootId, const QString &message) {
        m_lastMessage = message;
        emit importStateChanged();
        emit catalogChanged();
        emit sourceScanFailed(sourceRootId, message);
    });
}

bool ImportService::importDirectory(const QString &directoryPath, QString *errorMessage)
{
    const auto requestedPath = FolderPathMetadata::normalizeSourcePath(directoryPath);
    if (requestedPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请输入本地文件夹或网络共享路径。");
        }
        return false;
    }
    const QFileInfo requestedInfo(requestedPath);
    const auto normalizedPath = FolderPathMetadata::normalizeSourcePath(requestedInfo.absoluteFilePath());
    const QFileInfo info(normalizedPath);
    if (!info.exists() || !info.isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目录不存在或网络路径不可访问：%1").arg(directoryPath.trimmed());
        }
        return false;
    }
    if (!info.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目录不可读：%1").arg(directoryPath);
        }
        return false;
    }
    if (ScanPathPolicy::isExcludedPath(normalizedPath,
                                       normalizedPath,
                                       m_databaseManager->databaseFilePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "该路径属于系统临时区、应用缓存、项目生成目录或卷影副本，不能作为素材源：%1")
                                .arg(normalizedPath);
        }
        return false;
    }

    QSqlQuery duplicateQuery(m_databaseManager->database());
    duplicateQuery.prepare(QStringLiteral("SELECT path FROM source_root"));
    if (duplicateQuery.exec()) {
        while (duplicateQuery.next()) {
            const auto existingPath = duplicateQuery.value(0).toString();
            if (!ScanPathPolicy::pathsOverlap(existingPath, normalizedPath)) {
                continue;
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "素材源与已有素材源存在父子目录重叠，不能重复建立索引：%1")
                                    .arg(existingPath);
            }
            return false;
        }
    }
    if (ScanPathPolicy::isLinkOrReparsePoint(normalizedPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "素材源是符号链接、目录联接或挂载点。为避免循环扫描，默认不跟随此路径：%1")
                                .arg(normalizedPath);
        }
        return false;
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery insertQuery(m_databaseManager->database());
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO source_root (name, path, status, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?)"));
    const auto sourceName = FolderPathMetadata::folderName(normalizedPath, normalizedPath);
    insertQuery.addBindValue(sourceName);
    insertQuery.addBindValue(normalizedPath);
    insertQuery.addBindValue(QStringLiteral("scanning"));
    insertQuery.addBindValue(now);
    insertQuery.addBindValue(now);
    if (!insertQuery.exec()) {
        if (errorMessage) {
            *errorMessage = insertQuery.lastError().text();
        }
        return false;
    }

    SourceRoot sourceRoot;
    sourceRoot.id = insertQuery.lastInsertId().toLongLong();
    sourceRoot.name = sourceName;
    sourceRoot.path = normalizedPath;
    sourceRoot.status = QStringLiteral("scanning");

    if (!startSourceScan(sourceRoot,
                         QStringLiteral("扫描"),
                         QStringLiteral("准备扫描目录"),
                         {},
                         errorMessage)) {
        QSqlQuery cleanup(m_databaseManager->database());
        cleanup.prepare(QStringLiteral("DELETE FROM source_root WHERE id = ?"));
        cleanup.addBindValue(sourceRoot.id);
        cleanup.exec();
        return false;
    }

    m_lastMessage = QStringLiteral("已开始导入素材源：%1").arg(sourceRoot.name);
    emit importStateChanged();
    emit catalogChanged();
    emit sourceRootsChanged();
    return true;
}

QVector<SourceRoot> ImportService::sourceRoots() const
{
    QVector<SourceRoot> roots;
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        return roots;
    }

    QSqlQuery query(m_databaseManager->database());
    if (!query.exec(QStringLiteral(
            "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, COALESCE(scan_version, 0) "
            "FROM source_root ORDER BY id"))) {
        return roots;
    }
    while (query.next()) {
        roots.append(readSourceRoot(query));
    }
    return roots;
}

bool ImportService::rescanSourceRoot(qint64 sourceRootId,
                                     const QString &reason,
                                     QString *errorMessage)
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前没有打开的项目。");
        }
        return false;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, COALESCE(scan_version, 0) "
        "FROM source_root WHERE id = ?"));
    query.addBindValue(sourceRootId);
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text().trimmed().isEmpty()
                ? QStringLiteral("素材源不存在或已移除。")
                : query.lastError().text();
        }
        return false;
    }

    const auto sourceRoot = readSourceRoot(query);
    const QFileInfo info(sourceRoot.path);
    if (!info.exists() || !info.isDir() || !info.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材源当前不可访问：%1").arg(sourceRoot.path);
        }
        return false;
    }

    const auto detail = reason.trimmed().isEmpty()
        ? QStringLiteral("正在重新扫描素材源")
        : reason.trimmed();
    if (!startSourceScan(sourceRoot,
                         QStringLiteral("更新索引"),
                         detail,
                         {},
                         errorMessage)) {
        return false;
    }
    m_lastMessage = QStringLiteral("已开始后台更新索引：%1").arg(sourceRoot.name);
    emit importStateChanged();
    return true;
}

bool ImportService::rescanSourceDirectories(qint64 sourceRootId,
                                            const QStringList &changedPaths,
                                            bool forceFullScan,
                                            const QString &reason,
                                            QString *errorMessage)
{
    if (forceFullScan) {
        return rescanSourceRoot(sourceRootId, reason, errorMessage);
    }
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前没有打开的项目。");
        }
        return false;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, COALESCE(scan_version, 0) "
        "FROM source_root WHERE id = ?"));
    query.addBindValue(sourceRootId);
    if (!query.exec() || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text().trimmed().isEmpty()
                ? QStringLiteral("素材源不存在或已移除。")
                : query.lastError().text();
        }
        return false;
    }
    const auto sourceRoot = readSourceRoot(query);
    const auto dirtyDirectories = ScanPathPolicy::normalizeDirtyDirectories(
        sourceRoot.path,
        changedPaths,
        m_databaseManager->databaseFilePath());
    if (dirtyDirectories.isEmpty()) {
        QTimer::singleShot(0, this, [this, sourceRootId]() {
            emit sourceScanFinished(sourceRootId);
        });
        return true;
    }

    const auto detail = reason.trimmed().isEmpty()
        ? QStringLiteral("正在定向更新变化目录")
        : reason.trimmed();
    if (!startSourceScan(sourceRoot,
                         QStringLiteral("定向更新索引"),
                         detail,
                         dirtyDirectories,
                         errorMessage)) {
        return false;
    }
    m_lastMessage = QStringLiteral("已开始定向更新 %1 个变化目录：%2")
                        .arg(dirtyDirectories.size())
                        .arg(sourceRoot.name);
    emit importStateChanged();
    return true;
}

bool ImportService::startSourceScan(const SourceRoot &sourceRoot,
                                    const QString &titlePrefix,
                                    const QString &detail,
                                    const QStringList &dirtyDirectoryPaths,
                                    QString *errorMessage)
{
    if (!m_jobService || !m_jobService->engine() || !m_scanEngine) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("扫描服务尚未就绪。");
        }
        return false;
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery markScanning(m_databaseManager->database());
    markScanning.prepare(QStringLiteral("UPDATE source_root SET status = ?, updated_at = ? WHERE id = ?"));
    markScanning.addBindValue(QStringLiteral("scanning"));
    markScanning.addBindValue(now);
    markScanning.addBindValue(sourceRoot.id);
    if (!markScanning.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新素材源扫描状态失败：%1").arg(markScanning.lastError().text());
        }
        return false;
    }

    const auto jobId = m_jobService->engine()->createJob(JobType::Scan,
                                                         QStringLiteral("%1 %2").arg(titlePrefix, sourceRoot.name),
                                                         detail,
                                                         sourceRoot.id,
                                                         sourceRootJobSubject(sourceRoot),
                                                         scanProgressContext());
    m_scanEngine->startScan(sourceRoot, jobId, dirtyDirectoryPaths);
    return true;
}

QString ImportService::lastMessage() const
{
    return m_lastMessage;
}

void ImportService::resumeInterruptedScans()
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject() || !m_jobService || !m_jobService->engine() || !m_scanEngine) {
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    if (!query.exec(QStringLiteral(
            "SELECT sr.id, sr.name, sr.path, sr.status, sr.total_files, sr.total_folders, sr.total_size_bytes, "
            "sr.video_count, sr.audio_count, sr.image_count, sr.other_count, sr.warning_count, COALESCE(sr.scan_version, 0) "
            "FROM source_root sr JOIN scan_session ss ON ss.source_root_id = sr.id "
            "WHERE ss.state <> 'completed' ORDER BY sr.id"))) {
        m_lastMessage = QStringLiteral("检查可恢复扫描任务失败：%1").arg(query.lastError().text());
        emit importStateChanged();
        return;
    }

    QVector<SourceRoot> resumableRoots;
    QVector<SourceRoot> waitingRoots;
    while (query.next()) {
        const auto sourceRoot = readSourceRoot(query);
        const QFileInfo info(sourceRoot.path);
        if (info.exists() && info.isDir() && info.isReadable()) {
            resumableRoots.append(sourceRoot);
        } else {
            waitingRoots.append(sourceRoot);
        }
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const auto &sourceRoot : waitingRoots) {
        QSqlQuery markWaiting(m_databaseManager->database());
        markWaiting.prepare(QStringLiteral("UPDATE source_root SET status = 'warning', updated_at = ? WHERE id = ?"));
        markWaiting.addBindValue(now);
        markWaiting.addBindValue(sourceRoot.id);
        markWaiting.exec();
    }

    int resumed = 0;
    for (const auto &sourceRoot : resumableRoots) {
        QString errorMessage;
        if (startSourceScan(sourceRoot,
                            QStringLiteral("恢复扫描"),
                            QStringLiteral("从上次保存的目录检查点继续建立索引"),
                            {},
                            &errorMessage)) {
            ++resumed;
        }
    }

    if (resumed > 0) {
        m_lastMessage = QStringLiteral("已恢复 %1 个中断的素材扫描任务，将从最近目录检查点继续。").arg(resumed);
    } else if (!waitingRoots.isEmpty()) {
        m_lastMessage = QStringLiteral("有 %1 个中断扫描等待素材盘或网络目录重新接入。").arg(waitingRoots.size());
    } else {
        return;
    }
    emit importStateChanged();
    emit catalogChanged();
}

void ImportService::rescanLegacySourceRoots()
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject() || !m_jobService || !m_jobService->engine() || !m_scanEngine) {
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, COALESCE(scan_version, 0) "
        "FROM source_root WHERE COALESCE(scan_version, 0) < ? "
        "AND NOT EXISTS (SELECT 1 FROM scan_session ss WHERE ss.source_root_id = source_root.id AND ss.state <> 'completed') "
        "ORDER BY id"));
    query.addBindValue(ScanEngine::CurrentScanVersion);
    if (!query.exec()) {
        m_lastMessage = QStringLiteral("检查历史素材源失败：%1").arg(query.lastError().text());
        emit importStateChanged();
        return;
    }

    QVector<SourceRoot> legacyRoots;
    while (query.next()) {
        auto sourceRoot = readSourceRoot(query);
        const QFileInfo info(sourceRoot.path);
        if (sourceRoot.id > 0 && info.exists() && info.isDir() && info.isReadable()) {
            legacyRoots.append(sourceRoot);
        }
    }
    if (legacyRoots.isEmpty()) {
        return;
    }

    for (const auto &sourceRoot : legacyRoots) {
        startSourceScan(sourceRoot,
                        QStringLiteral("补扫历史素材源"),
                        QStringLiteral("正在升级旧素材目录索引为全部文件"),
                        {});
    }

    m_lastMessage = QStringLiteral("已开始补扫 %1 个历史素材源，完成后素材管理中心会显示全部文件。").arg(legacyRoots.size());
    emit importStateChanged();
    emit catalogChanged();
}
