#include "application/ImportService.h"

#include "application/JobService.h"
#include "core/jobs/JobEngine.h"
#include "core/scan/ScanEngine.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

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
    connect(m_scanEngine, &ScanEngine::scanBatchCommitted, this, &ImportService::catalogChanged);
    connect(m_scanEngine, &ScanEngine::scanFinished, this, &ImportService::catalogChanged);
    connect(m_scanEngine, &ScanEngine::scanFailed, this, [this](qint64, const QString &message) {
        m_lastMessage = message;
        emit importStateChanged();
        emit catalogChanged();
    });
}

bool ImportService::importDirectory(const QString &directoryPath, QString *errorMessage)
{
    const QFileInfo info(directoryPath);
    if (!info.exists() || !info.isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目录不存在：%1").arg(directoryPath);
        }
        return false;
    }
    if (!info.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目录不可读：%1").arg(directoryPath);
        }
        return false;
    }

    QSqlQuery duplicateQuery(m_databaseManager->database());
    duplicateQuery.prepare(QStringLiteral("SELECT id FROM source_root WHERE path = ?"));
    duplicateQuery.addBindValue(info.absoluteFilePath());
    if (duplicateQuery.exec() && duplicateQuery.next()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("素材源已存在：%1").arg(info.absoluteFilePath());
        }
        return false;
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery insertQuery(m_databaseManager->database());
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO source_root (name, path, status, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?)"));
    insertQuery.addBindValue(info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName());
    insertQuery.addBindValue(info.absoluteFilePath());
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
    sourceRoot.name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
    sourceRoot.path = info.absoluteFilePath();
    sourceRoot.status = QStringLiteral("scanning");

    const auto jobId = m_jobService->engine()->createJob(JobType::Scan,
                                                         QStringLiteral("扫描 %1").arg(sourceRoot.name),
                                                         QStringLiteral("准备扫描目录"),
                                                         sourceRoot.id,
                                                         sourceRootJobSubject(sourceRoot),
                                                         scanProgressContext());
    m_scanEngine->startScan(sourceRoot, jobId);

    m_lastMessage = QStringLiteral("已开始导入素材源：%1").arg(sourceRoot.name);
    emit importStateChanged();
    emit catalogChanged();
    return true;
}

QString ImportService::lastMessage() const
{
    return m_lastMessage;
}

void ImportService::rescanLegacySourceRoots()
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject() || !m_jobService || !m_jobService->engine() || !m_scanEngine) {
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, COALESCE(scan_version, 0) "
        "FROM source_root WHERE COALESCE(scan_version, 0) < ? ORDER BY id"));
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

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const auto &sourceRoot : legacyRoots) {
        QSqlQuery markScanning(m_databaseManager->database());
        markScanning.prepare(QStringLiteral("UPDATE source_root SET status = ?, updated_at = ? WHERE id = ?"));
        markScanning.addBindValue(QStringLiteral("scanning"));
        markScanning.addBindValue(now);
        markScanning.addBindValue(sourceRoot.id);
        markScanning.exec();

        const auto jobId = m_jobService->engine()->createJob(JobType::Scan,
                                                             QStringLiteral("补扫历史素材源 %1").arg(sourceRoot.name),
                                                             QStringLiteral("正在升级旧素材目录索引为全部文件"),
                                                             sourceRoot.id,
                                                             sourceRootJobSubject(sourceRoot),
                                                             scanProgressContext());
        m_scanEngine->startScan(sourceRoot, jobId);
    }

    m_lastMessage = QStringLiteral("已开始补扫 %1 个历史素材源，完成后素材管理中心会显示全部文件。").arg(legacyRoots.size());
    emit importStateChanged();
    emit catalogChanged();
}
