#include "application/MaterialCatalogSyncService.h"

#include "application/ProjectService.h"
#include "core/jobs/JobEngine.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Formatters.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QMetaObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace {
struct ExistingVideoState {
    QString videoKey;
    QString absolutePath;
    qint64 sizeBytes = 0;
    QString modifiedAt;
    VideoAnalysisStatus analysisStatus = VideoAnalysisStatus::Pending;
    ConfirmationStatus confirmationStatus = ConfirmationStatus::Pending;
    QString sourceText;
    QString errorMessage;
};

bool isSupportedTextAsset(AssetType assetType, const QString &extension)
{
    static const QSet<QString> textExtensions = {
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("md"),
        QStringLiteral("json"), QStringLiteral("csv"), QStringLiteral("tsv"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("docx"), QStringLiteral("xlsx"), QStringLiteral("pptx"),
        QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("vtt")
    };
    const auto normalizedExtension = extension.trimmed().toLower();
    return assetType == AssetType::Subtitle
        || (assetType == AssetType::Document && textExtensions.contains(normalizedExtension));
}

bool canAnalyzeAsset(AssetType assetType, const QString &extension)
{
    return assetType == AssetType::Video
        || assetType == AssetType::Image
        || isSupportedTextAsset(assetType, extension);
}

VideoAnalysisStatus initialAnalysisStatusForAsset(AssetType assetType, const QString &extension)
{
    return canAnalyzeAsset(assetType, extension)
        ? VideoAnalysisStatus::Pending
        : VideoAnalysisStatus::IndexedOnly;
}

QString buildTechnicalSummary(const QString &container, qint64 durationMs, qint64 bitRate)
{
    QStringList parts;
    if (!container.trimmed().isEmpty()) {
        parts.append(container.trimmed());
    }
    if (durationMs > 0) {
        parts.append(Formatters::formatDuration(durationMs));
    }
    if (bitRate > 0) {
        parts.append(Formatters::formatBitRate(bitRate));
    }
    return parts.join(QStringLiteral(" · "));
}

JobSubject projectJobSubject(const Project &project, const QString &fallbackName = QString())
{
    JobSubject subject;
    subject.kind = QStringLiteral("project");
    subject.key = project.id;
    subject.name = project.name.trimmed().isEmpty() ? fallbackName : project.name;
    subject.path = project.rootPath;
    subject.typeLabel = QStringLiteral("项目");
    return subject;
}

JobProgressContext projectProgressContext(const QString &stepLabel, qint64 current, qint64 total)
{
    JobProgressContext context;
    context.currentStep = 1;
    context.totalSteps = 1;
    context.stepLabel = stepLabel;
    context.currentItem = current;
    context.totalItems = total;
    context.unitLabel = QStringLiteral("个项目");
    return context;
}

QString emptyIfNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString stablePathKey(QString path)
{
    auto normalized = QDir::cleanPath(path.trimmed());
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized.toCaseFolded();
}

bool execQuery(QSqlQuery &query, QString *errorMessage)
{
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

QSqlDatabase openProjectConnection(const QString &databasePath, const QString &connectionName, QString *errorMessage)
{
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(databasePath);
    if (!db.open() && errorMessage) {
        *errorMessage = db.lastError().text();
    }
    return db;
}

void closeProjectConnection(const QString &connectionName)
{
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }
    {
        auto db = QSqlDatabase::database(connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QVector<GlobalVideoAsset> fetchProjectAssets(QSqlDatabase &projectDb, const Project &project, QString *errorMessage)
{
    QVector<GlobalVideoAsset> assets;
    QSqlQuery query(projectDb);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, COALESCE(sr.name, ''), af.name, COALESCE(af.extension, ''), "
        "af.absolute_path, af.relative_path, af.asset_type, af.size_bytes, af.modified_at, "
        "COALESCE(mm.duration_ms, 0), COALESCE(mm.container, ''), COALESCE(mm.bit_rate, 0), "
        "CASE WHEN COALESCE(th.status, 0) = 1 THEN COALESCE(th.image_path, '') ELSE '' END, COALESCE(th.status, 0) "
        "FROM asset_file af "
        "LEFT JOIN source_root sr ON sr.id = af.source_root_id "
        "LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        "LEFT JOIN thumbnail th ON th.asset_id = af.id "
        "ORDER BY af.id"));
    if (!execQuery(query, errorMessage)) {
        return assets;
    }

    while (query.next()) {
        GlobalVideoAsset asset;
        asset.projectUuid = project.id;
        asset.projectName = project.name;
        asset.projectDatabasePath = project.databasePath;
        asset.assetId = query.value(0).toLongLong();
        asset.sourceRootId = query.value(1).toLongLong();
        asset.sourceRootName = query.value(2).toString();
        asset.fileName = query.value(3).toString();
        asset.extension = query.value(4).toString();
        asset.absolutePath = query.value(5).toString();
        asset.relativePath = query.value(6).toString();
        asset.assetType = static_cast<AssetType>(query.value(7).toInt());
        asset.sizeBytes = query.value(8).toLongLong();
        asset.modifiedAt = query.value(9).toString();
        asset.durationMs = query.value(10).toLongLong();
        asset.technicalSummary = buildTechnicalSummary(query.value(11).toString(), asset.durationMs, query.value(12).toLongLong());
        asset.thumbnailPath = query.value(13).toString();
        asset.thumbnailStatus = static_cast<ThumbnailStatus>(query.value(14).toInt());
        asset.videoKey = QStringLiteral("%1:%2").arg(project.id).arg(asset.assetId);
        asset.assetKey = asset.videoKey;
        assets.append(asset);
    }
    return assets;
}

QHash<QString, ExistingVideoState> loadExistingStates(QSqlDatabase &globalDb, const QString &projectUuid, QString *errorMessage)
{
    QHash<QString, ExistingVideoState> states;
    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral(
        "SELECT video_key, COALESCE(absolute_path, ''), size_bytes, modified_at, analysis_status, confirmation_status, "
        "COALESCE(source_text, ''), COALESCE(error_message, '') "
        "FROM global_video_asset WHERE project_uuid = ?"));
    query.addBindValue(projectUuid);
    if (!execQuery(query, errorMessage)) {
        return states;
    }

    while (query.next()) {
        ExistingVideoState state;
        state.videoKey = query.value(0).toString();
        state.absolutePath = query.value(1).toString();
        state.sizeBytes = query.value(2).toLongLong();
        state.modifiedAt = query.value(3).toString();
        state.analysisStatus = static_cast<VideoAnalysisStatus>(query.value(4).toInt());
        state.confirmationStatus = static_cast<ConfirmationStatus>(query.value(5).toInt());
        state.sourceText = query.value(6).toString();
        state.errorMessage = query.value(7).toString();
        states.insert(state.videoKey, state);
    }
    return states;
}

bool updateProjectRegistry(QSqlDatabase &globalDb,
                           const Project &project,
                           const QString &status,
                           const QString &errorMessageText,
                           QString *errorMessage)
{
    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral(
        "INSERT INTO project_registry (project_uuid, project_name, project_database_path, last_synced_at, sync_status, error_message) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(project_uuid) DO UPDATE SET "
        "project_name = excluded.project_name, "
        "project_database_path = excluded.project_database_path, "
        "last_synced_at = excluded.last_synced_at, "
        "sync_status = excluded.sync_status, "
        "error_message = excluded.error_message"));
    query.addBindValue(project.id);
    query.addBindValue(project.name);
    query.addBindValue(project.databasePath);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(status);
    query.addBindValue(errorMessageText);
    return execQuery(query, errorMessage);
}

bool deleteFtsRow(QSqlDatabase &globalDb, const QString &videoKey, bool hasFts5, QString *errorMessage)
{
    if (!hasFts5) {
        return true;
    }
    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral("DELETE FROM video_search_fts WHERE video_key = ?"));
    query.addBindValue(videoKey);
    return execQuery(query, errorMessage);
}

bool upsertSearchRow(QSqlDatabase &globalDb, const GlobalVideoAsset &asset, bool hasFts5, QString *errorMessage)
{
    if (!hasFts5) {
        return true;
    }
    if (!deleteFtsRow(globalDb, asset.videoKey, hasFts5, errorMessage)) {
        return false;
    }

    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral(
        "INSERT INTO video_search_fts "
        "(video_key, project_name, source_root_name, file_name, relative_path, absolute_path, "
        "asset_type_label, extension, technical_summary, summary, keywords, captions, source_text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(asset.videoKey);
    query.addBindValue(asset.projectName);
    query.addBindValue(asset.sourceRootName);
    query.addBindValue(asset.fileName);
    query.addBindValue(asset.relativePath);
    query.addBindValue(asset.absolutePath);
    query.addBindValue(Formatters::assetTypeLabel(asset.assetType));
    query.addBindValue(asset.extension);
    query.addBindValue(emptyIfNull(asset.technicalSummary));
    query.addBindValue(emptyIfNull(asset.summary));
    query.addBindValue(asset.keywords.join(QStringLiteral(" ")));
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(emptyIfNull(asset.sourceText));
    return execQuery(query, errorMessage);
}

bool migrateAnalysisArtifacts(QSqlDatabase &globalDb,
                              const QString &oldVideoKey,
                              const QString &newVideoKey,
                              bool hasFts5,
                              QString *errorMessage)
{
    if (oldVideoKey == newVideoKey) {
        return true;
    }

    const QStringList deleteStatements = {
        QStringLiteral("DELETE FROM material_dimension_frame_analysis WHERE video_key = ?"),
        QStringLiteral("DELETE FROM video_frame_analysis WHERE video_key = ?"),
        QStringLiteral("DELETE FROM video_analysis_result WHERE video_key = ?"),
        QStringLiteral("DELETE FROM video_analysis_task WHERE video_key = ?"),
        QStringLiteral("DELETE FROM material_dimension_analysis WHERE video_key = ?")
    };
    for (const auto &statement : deleteStatements) {
        QSqlQuery query(globalDb);
        query.prepare(statement);
        query.addBindValue(newVideoKey);
        if (!execQuery(query, errorMessage)) {
            return false;
        }
    }

    if (hasFts5) {
        QSqlQuery deleteTargetFts(globalDb);
        deleteTargetFts.prepare(QStringLiteral("DELETE FROM video_search_fts WHERE video_key = ?"));
        deleteTargetFts.addBindValue(newVideoKey);
        if (!execQuery(deleteTargetFts, errorMessage)) {
            return false;
        }
    }

    const QStringList updateStatements = {
        QStringLiteral("UPDATE material_dimension_frame_analysis SET video_key = ? WHERE video_key = ?"),
        QStringLiteral("UPDATE video_frame_analysis SET video_key = ? WHERE video_key = ?"),
        QStringLiteral("UPDATE video_analysis_result SET video_key = ? WHERE video_key = ?"),
        QStringLiteral("UPDATE video_analysis_task SET video_key = ? WHERE video_key = ?"),
        QStringLiteral("UPDATE material_dimension_analysis SET video_key = ? WHERE video_key = ?")
    };
    for (const auto &statement : updateStatements) {
        QSqlQuery query(globalDb);
        query.prepare(statement);
        query.addBindValue(newVideoKey);
        query.addBindValue(oldVideoKey);
        if (!execQuery(query, errorMessage)) {
            return false;
        }
    }

    if (hasFts5) {
        QSqlQuery updateFts(globalDb);
        updateFts.prepare(QStringLiteral("UPDATE video_search_fts SET video_key = ? WHERE video_key = ?"));
        updateFts.addBindValue(newVideoKey);
        updateFts.addBindValue(oldVideoKey);
        if (!execQuery(updateFts, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool syncProjectIntoGlobal(QSqlDatabase &globalDb,
                           const Project &project,
                           bool hasFts5,
                           QString *errorMessage)
{
    const auto projectConnectionName = QStringLiteral("sync_project_%1_%2")
        .arg(project.id)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString openError;
    auto projectDb = openProjectConnection(project.databasePath, projectConnectionName, &openError);
    if (!projectDb.isOpen()) {
        if (errorMessage) {
            *errorMessage = openError;
        }
        closeProjectConnection(projectConnectionName);
        return false;
    }

    QString fetchError;
    const auto assets = fetchProjectAssets(projectDb, project, &fetchError);
    closeProjectConnection(projectConnectionName);
    if (!fetchError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = fetchError;
        }
        return false;
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    const auto existingStates = loadExistingStates(globalDb, project.id, errorMessage);
    if (errorMessage && !errorMessage->isEmpty()) {
        return false;
    }
    QHash<QString, ExistingVideoState> existingStatesByPath;
    for (auto it = existingStates.cbegin(); it != existingStates.cend(); ++it) {
        const auto key = stablePathKey(it.value().absolutePath);
        if (!key.isEmpty() && !existingStatesByPath.contains(key)) {
            existingStatesByPath.insert(key, it.value());
        }
    }

    if (!globalDb.transaction()) {
        if (errorMessage) {
            *errorMessage = globalDb.lastError().text();
        }
        return false;
    }

    if (!updateProjectRegistry(globalDb, project, QStringLiteral("syncing"), QString(), errorMessage)) {
        globalDb.rollback();
        return false;
    }

    QSet<QString> currentKeys;
    QSqlQuery clearFrames(globalDb);
    clearFrames.prepare(QStringLiteral("DELETE FROM video_frame_analysis WHERE video_key = ?"));
    QSqlQuery clearResult(globalDb);
    clearResult.prepare(QStringLiteral("DELETE FROM video_analysis_result WHERE video_key = ?"));
    QSqlQuery clearDimensions(globalDb);
    clearDimensions.prepare(QStringLiteral("DELETE FROM material_dimension_analysis WHERE video_key = ?"));
    QSqlQuery clearDimensionFrames(globalDb);
    clearDimensionFrames.prepare(QStringLiteral("DELETE FROM material_dimension_frame_analysis WHERE video_key = ?"));
    QSqlQuery upsert(globalDb);
    upsert.prepare(QStringLiteral(
        "INSERT INTO global_video_asset "
        "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, asset_id, "
        "file_name, extension, absolute_path, relative_path, asset_type, size_bytes, modified_at, duration_ms, "
        "thumbnail_path, thumbnail_status, analysis_status, confirmation_status, technical_summary, source_text, "
        "error_message, last_synced_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key) DO UPDATE SET "
        "project_uuid = excluded.project_uuid, "
        "project_name = excluded.project_name, "
        "project_database_path = excluded.project_database_path, "
        "source_root_id = excluded.source_root_id, "
        "source_root_name = excluded.source_root_name, "
        "asset_id = excluded.asset_id, "
        "file_name = excluded.file_name, "
        "extension = excluded.extension, "
        "absolute_path = excluded.absolute_path, "
        "relative_path = excluded.relative_path, "
        "asset_type = excluded.asset_type, "
        "size_bytes = excluded.size_bytes, "
        "modified_at = excluded.modified_at, "
        "duration_ms = excluded.duration_ms, "
        "thumbnail_path = excluded.thumbnail_path, "
        "thumbnail_status = excluded.thumbnail_status, "
        "analysis_status = excluded.analysis_status, "
        "confirmation_status = excluded.confirmation_status, "
        "technical_summary = excluded.technical_summary, "
        "source_text = CASE WHEN global_video_asset.size_bytes != excluded.size_bytes "
        "OR global_video_asset.modified_at != excluded.modified_at THEN excluded.source_text ELSE global_video_asset.source_text END, "
        "error_message = excluded.error_message, "
        "last_synced_at = excluded.last_synced_at, "
        "updated_at = excluded.updated_at"));

    QSet<QString> claimedExistingKeys;
    for (auto asset : assets) {
        currentKeys.insert(asset.videoKey);
        const bool exactMatch = existingStates.contains(asset.videoKey);
        auto existing = existingStates.value(asset.videoKey);
        bool remappedExisting = false;
        if (!exactMatch) {
            const auto pathKey = stablePathKey(asset.absolutePath);
            const auto pathMatch = existingStatesByPath.constFind(pathKey);
            if (pathMatch != existingStatesByPath.cend() && !claimedExistingKeys.contains(pathMatch.value().videoKey)) {
                existing = pathMatch.value();
                remappedExisting = true;
            }
        }
        const bool hasExisting = exactMatch || remappedExisting;
        if (hasExisting) {
            claimedExistingKeys.insert(existing.videoKey);
        }
        const bool changed = !hasExisting
            || existing.sizeBytes != asset.sizeBytes
            || existing.modifiedAt != asset.modifiedAt;
        asset.technicalSummary = emptyIfNull(asset.technicalSummary);
        asset.sourceText = changed ? QStringLiteral("") : emptyIfNull(existing.sourceText);
        const auto errorMessageText = changed ? QStringLiteral("") : emptyIfNull(existing.errorMessage);

        if (changed) {
            clearFrames.addBindValue(asset.videoKey);
            if (!execQuery(clearFrames, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            clearFrames.finish();

            clearResult.addBindValue(asset.videoKey);
            if (!execQuery(clearResult, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            clearResult.finish();

            clearDimensions.addBindValue(asset.videoKey);
            if (!execQuery(clearDimensions, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            clearDimensions.finish();

            clearDimensionFrames.addBindValue(asset.videoKey);
            if (!execQuery(clearDimensionFrames, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            clearDimensionFrames.finish();

            if (!deleteFtsRow(globalDb, asset.videoKey, hasFts5, errorMessage)) {
                globalDb.rollback();
                return false;
            }
        }

        upsert.addBindValue(asset.videoKey);
        upsert.addBindValue(project.id);
        upsert.addBindValue(project.name);
        upsert.addBindValue(project.databasePath);
        upsert.addBindValue(asset.sourceRootId);
        upsert.addBindValue(asset.sourceRootName);
        upsert.addBindValue(asset.assetId);
        upsert.addBindValue(asset.fileName);
        upsert.addBindValue(asset.extension);
        upsert.addBindValue(asset.absolutePath);
        upsert.addBindValue(asset.relativePath);
        upsert.addBindValue(static_cast<int>(asset.assetType));
        upsert.addBindValue(asset.sizeBytes);
        upsert.addBindValue(asset.modifiedAt);
        upsert.addBindValue(asset.durationMs);
        upsert.addBindValue(asset.thumbnailPath);
        upsert.addBindValue(static_cast<int>(asset.thumbnailStatus));
        upsert.addBindValue(static_cast<int>(changed ? initialAnalysisStatusForAsset(asset.assetType, asset.extension) : existing.analysisStatus));
        upsert.addBindValue(static_cast<int>(changed ? ConfirmationStatus::Pending : existing.confirmationStatus));
        upsert.addBindValue(asset.technicalSummary);
        upsert.addBindValue(asset.sourceText);
        upsert.addBindValue(errorMessageText);
        upsert.addBindValue(now);
        upsert.addBindValue(now);
        if (!execQuery(upsert, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        upsert.finish();

        if (remappedExisting && !changed
            && !migrateAnalysisArtifacts(globalDb, existing.videoKey, asset.videoKey, hasFts5, errorMessage)) {
            globalDb.rollback();
            return false;
        }

        if (changed && !upsertSearchRow(globalDb, asset, hasFts5, errorMessage)) {
            globalDb.rollback();
            return false;
        }
    }

    for (auto it = existingStates.cbegin(); it != existingStates.cend(); ++it) {
        if (currentKeys.contains(it.key())) {
            continue;
        }

        QSqlQuery deleteAsset(globalDb);
        deleteAsset.prepare(QStringLiteral("DELETE FROM global_video_asset WHERE video_key = ?"));
        deleteAsset.addBindValue(it.key());
        if (!execQuery(deleteAsset, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        if (!deleteFtsRow(globalDb, it.key(), hasFts5, errorMessage)) {
            globalDb.rollback();
            return false;
        }
    }

    if (!updateProjectRegistry(globalDb, project, QStringLiteral("ok"), QString(), errorMessage)) {
        globalDb.rollback();
        return false;
    }

    if (!globalDb.commit()) {
        if (errorMessage) {
            *errorMessage = globalDb.lastError().text();
        }
        globalDb.rollback();
        return false;
    }
    return true;
}

QVector<Project> loadRegisteredProjects(QSqlDatabase &globalDb, QString *errorMessage)
{
    QVector<Project> projects;
    QSqlQuery read(globalDb);
    read.prepare(QStringLiteral(
        "SELECT project_uuid, project_name, project_database_path FROM project_registry ORDER BY project_name COLLATE NOCASE"));
    if (!execQuery(read, errorMessage)) {
        return projects;
    }

    while (read.next()) {
        Project project;
        project.id = read.value(0).toString();
        project.name = read.value(1).toString();
        project.databasePath = read.value(2).toString();
        projects.append(project);
    }
    return projects;
}
}

#ifdef CINEVAULT_TESTING
bool syncProjectIntoGlobalForTest(QSqlDatabase &globalDb, const Project &project, bool hasFts5, QString *errorMessage)
{
    return syncProjectIntoGlobal(globalDb, project, hasFts5, errorMessage);
}
#endif

MaterialCatalogSyncService::MaterialCatalogSyncService(GlobalDatabaseManager *globalDatabaseManager,
                                                       JobEngine *jobEngine,
                                                       ProjectService *projectService,
                                                       QObject *parent)
    : QObject(parent)
    , m_globalDatabaseManager(globalDatabaseManager)
    , m_jobEngine(jobEngine)
    , m_projectService(projectService)
{
}

void MaterialCatalogSyncService::syncCurrentProject()
{
    if (!m_globalDatabaseManager || !m_projectService || !m_projectService->hasOpenProject()) {
        return;
    }
    if (m_syncRunning.exchange(true)) {
        m_syncPending = true;
        return;
    }
    m_syncPending = false;

    const auto project = m_projectService->currentProject();
    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::GlobalSync,
                                 QStringLiteral("同步全局索引 %1").arg(project.name),
                                 QStringLiteral("准备同步当前项目素材到素材管理中心"),
                                 0,
                                 projectJobSubject(project),
                                 projectProgressContext(QStringLiteral("同步项目索引"), 0, 1))
        : 0;

    auto future = QtConcurrent::run([this, project, jobId]() {
        const auto connectionName = QStringLiteral("global_sync_%1_%2")
            .arg(project.id)
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QString errorMessage;
        auto db = m_globalDatabaseManager->openThreadConnection(connectionName, &errorMessage);
        if (!db.isOpen()) {
            failJob(jobId, errorMessage);
            m_globalDatabaseManager->closeThreadConnection(connectionName);
            finishSyncRun();
            return;
        }

        updateJob(jobId, 25, QStringLiteral("正在读取项目素材索引"), projectProgressContext(QStringLiteral("同步项目索引"), 0, 1));
        if (!syncProjectIntoGlobal(db, project, m_globalDatabaseManager->hasFts5(), &errorMessage)) {
            failJob(jobId, errorMessage);
        } else {
            updateJob(jobId, 100, QStringLiteral("当前项目素材已同步到素材管理中心"), projectProgressContext(QStringLiteral("同步项目索引"), 1, 1));
            completeJob(jobId, QStringLiteral("当前项目素材已同步到素材管理中心"));
            notifyCatalogChanged();
        }
        m_globalDatabaseManager->closeThreadConnection(connectionName);
        finishSyncRun();
    });
    Q_UNUSED(future);
}

void MaterialCatalogSyncService::rebuildAllProjects()
{
    if (m_syncRunning.exchange(true) || !m_globalDatabaseManager) {
        return;
    }

    JobSubject catalogSubject;
    catalogSubject.kind = QStringLiteral("projectCatalog");
    catalogSubject.key = QStringLiteral("all");
    catalogSubject.name = QStringLiteral("全部已登记项目");
    catalogSubject.typeLabel = QStringLiteral("项目索引");

    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::GlobalSync,
                                 QStringLiteral("重建全局素材索引"),
                                 QStringLiteral("准备重建所有已登记项目的素材索引"),
                                 0,
                                 catalogSubject,
                                 projectProgressContext(QStringLiteral("重建项目索引"), 0, 0))
        : 0;

    auto future = QtConcurrent::run([this, jobId]() {
        const auto connectionName = QStringLiteral("global_rebuild_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QString errorMessage;
        auto db = m_globalDatabaseManager->openThreadConnection(connectionName, &errorMessage);
        if (!db.isOpen()) {
            failJob(jobId, errorMessage);
            m_globalDatabaseManager->closeThreadConnection(connectionName);
            finishSyncRun();
            return;
        }

        const auto projects = loadRegisteredProjects(db, &errorMessage);
        if (!errorMessage.isEmpty()) {
            failJob(jobId, errorMessage);
            m_globalDatabaseManager->closeThreadConnection(connectionName);
            finishSyncRun();
            return;
        }
        if (projects.isEmpty()) {
            completeJob(jobId, QStringLiteral("没有可重建的已登记项目"));
            m_globalDatabaseManager->closeThreadConnection(connectionName);
            finishSyncRun();
            return;
        }

        int successCount = 0;
        int failedCount = 0;
        for (int index = 0; index < projects.size(); ++index) {
            const auto &project = projects.at(index);
            updateJob(jobId,
                      qBound<qint64>(qint64{1},
                                     (static_cast<qint64>(index) * qint64{100}) / static_cast<qint64>(projects.size()),
                                     qint64{99}),
                      QStringLiteral("正在重建：%1").arg(project.name),
                      projectProgressContext(QStringLiteral("重建项目索引"), index + 1, projects.size()));

            QString syncError;
            if (syncProjectIntoGlobal(db, project, m_globalDatabaseManager->hasFts5(), &syncError)) {
                ++successCount;
            } else {
                ++failedCount;
                updateProjectRegistry(db, project, QStringLiteral("failed"), syncError, nullptr);
            }
        }

        if (failedCount > 0) {
            updateJob(jobId, 100, QStringLiteral("全局索引重建完成，成功 %1 个项目，失败 %2 个项目").arg(successCount).arg(failedCount), projectProgressContext(QStringLiteral("重建项目索引"), projects.size(), projects.size()));
            completeJob(jobId, QStringLiteral("全局索引重建完成，成功 %1 个项目，失败 %2 个项目").arg(successCount).arg(failedCount));
        } else {
            updateJob(jobId, 100, QStringLiteral("全局索引重建完成，共 %1 个项目").arg(successCount), projectProgressContext(QStringLiteral("重建项目索引"), projects.size(), projects.size()));
            completeJob(jobId, QStringLiteral("全局索引重建完成，共 %1 个项目").arg(successCount));
        }
        notifyCatalogChanged();
        m_globalDatabaseManager->closeThreadConnection(connectionName);
        finishSyncRun();
    });
    Q_UNUSED(future);
}

void MaterialCatalogSyncService::updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, jobId, progress, detail, progressContext]() {
        engine->updateJob(jobId, progress, detail, progressContext);
    }, Qt::QueuedConnection);
}

void MaterialCatalogSyncService::completeJob(qint64 jobId, const QString &detail)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, jobId, detail]() {
        engine->completeJob(jobId, detail);
    }, Qt::QueuedConnection);
}

void MaterialCatalogSyncService::failJob(qint64 jobId, const QString &errorMessage)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, jobId, errorMessage]() {
        engine->failJob(jobId, errorMessage);
    }, Qt::QueuedConnection);
}

void MaterialCatalogSyncService::finishSyncRun()
{
    m_syncRunning = false;
    if (m_syncPending.exchange(false)) {
        QMetaObject::invokeMethod(this, &MaterialCatalogSyncService::syncCurrentProject, Qt::QueuedConnection);
    }
}

void MaterialCatalogSyncService::notifyCatalogChanged()
{
    QMetaObject::invokeMethod(this, [this]() {
        emit catalogChanged();
    }, Qt::QueuedConnection);
}
