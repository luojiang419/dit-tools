#include "application/MaterialCatalogSyncService.h"

#include "application/ProjectService.h"
#include "core/jobs/JobEngine.h"
#include "core/search/CaptureTimeResolver.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/FolderPathMetadata.h"
#include "shared/Formatters.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QRegularExpression>
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

struct ProjectFolderState {
    QString folderKey;
    QString projectUuid;
    QString projectName;
    QString projectDatabasePath;
    qint64 sourceRootId = 0;
    QString sourceRootName;
    QString sourceRootPath;
    qint64 folderId = 0;
    QString name;
    QString absolutePath;
    QString pathKey;
    QString relativePath;
    QString parentRelativePath;
    int depth = 0;
    qint64 directFileCount = 0;
    qint64 recursiveFileCount = 0;
    QString normalizedDate;
    QString dateAnchor;
    bool available = true;
};

bool isSupportedTextAsset(AssetType assetType, const QString &extension)
{
    static const QSet<QString> textExtensions = {
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("md"),
        QStringLiteral("json"), QStringLiteral("csv"), QStringLiteral("tsv"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("pdf"),
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

QString buildEmbeddedMetadataSummary(const QString &cameraMake,
                                     const QString &cameraModel,
                                     const QString &lensModel,
                                     int width,
                                     int height,
                                     double frameRate,
                                     const QString &videoCodec,
                                     const QString &colorSpace,
                                     int sampleRate,
                                     int channels,
                                     const QString &timecode)
{
    QStringList parts;
    const auto camera = QStringList{cameraMake.trimmed(), cameraModel.trimmed()}
                            .filter(QRegularExpression(QStringLiteral(".+")))
                            .join(QLatin1Char(' '));
    if (!camera.isEmpty()) parts.append(camera);
    if (!lensModel.trimmed().isEmpty()) parts.append(lensModel.trimmed());
    if (width > 0 && height > 0) parts.append(QStringLiteral("%1×%2").arg(width).arg(height));
    if (frameRate > 0) parts.append(QStringLiteral("%1 fps").arg(frameRate, 0, 'f', frameRate < 10 ? 2 : 1));
    if (!videoCodec.trimmed().isEmpty()) parts.append(videoCodec.trimmed());
    if (!colorSpace.trimmed().isEmpty()) parts.append(colorSpace.trimmed());
    if (sampleRate > 0) parts.append(QStringLiteral("%1 Hz").arg(sampleRate));
    if (channels > 0) parts.append(QStringLiteral("%1 声道").arg(channels));
    if (!timecode.trimmed().isEmpty()) parts.append(QStringLiteral("TC %1").arg(timecode.trimmed()));
    parts.removeDuplicates();
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
    return FolderPathMetadata::normalizedPathKey(path);
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
    const QFileInfo databaseInfo(databasePath);
    if (!databaseInfo.exists() || !databaseInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目数据库当前离线：%1").arg(databasePath);
        }
        return {};
    }
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(databasePath);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
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

bool hasUnstableSourceRoot(QSqlDatabase &projectDb, QString *errorMessage)
{
    QSqlQuery query(projectDb);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM source_root WHERE LOWER(TRIM(COALESCE(status, ''))) NOT IN ('ok', 'warning') LIMIT 1"));
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    return query.next();
}

QVector<GlobalVideoAsset> fetchProjectAssets(QSqlDatabase &projectDb, const Project &project, QString *errorMessage)
{
    QVector<GlobalVideoAsset> assets;
    QSqlQuery query(projectDb);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, COALESCE(sr.name, ''), COALESCE(sr.path, ''), af.name, COALESCE(af.extension, ''), "
        "af.absolute_path, af.relative_path, af.asset_type, af.size_bytes, af.modified_at, "
        "COALESCE(mm.duration_ms, 0), COALESCE(mm.container, ''), COALESCE(mm.bit_rate, 0), COALESCE(mm.raw_json, ''), "
        "COALESCE(em.capture_time, ''), COALESCE(em.camera_make, ''), COALESCE(em.camera_model, ''), "
        "COALESCE(em.lens_model, ''), COALESCE(em.width, 0), COALESCE(em.height, 0), COALESCE(em.frame_rate, 0), "
        "COALESCE(em.video_codec, ''), COALESCE(em.color_space, ''), COALESCE(em.sample_rate, 0), "
        "COALESCE(em.channels, 0), COALESCE(em.timecode, ''), COALESCE(em.search_text, ''), "
        "CASE WHEN COALESCE(th.status, 0) = 1 THEN COALESCE(th.image_path, '') ELSE '' END, COALESCE(th.status, 0), "
        "af.is_readable "
        "FROM asset_file af "
        "LEFT JOIN source_root sr ON sr.id = af.source_root_id "
        "LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        "LEFT JOIN embedded_metadata em ON em.asset_id = af.id AND em.status = 1 "
        "LEFT JOIN thumbnail th ON th.asset_id = af.id "
        "ORDER BY af.id"));
    if (!execQuery(query, errorMessage)) {
        return assets;
    }

    QHash<QString, bool> sourceAvailability;
    CaptureTimeResolver captureTimeResolver;
    while (query.next()) {
        GlobalVideoAsset asset;
        asset.projectUuid = project.id;
        asset.projectName = project.name;
        asset.projectDatabasePath = project.databasePath;
        asset.assetId = query.value(0).toLongLong();
        asset.sourceRootId = query.value(1).toLongLong();
        asset.sourceRootName = query.value(2).toString();
        const auto sourceRootPath = query.value(3).toString();
        asset.fileName = query.value(4).toString();
        asset.extension = query.value(5).toString();
        asset.absolutePath = query.value(6).toString();
        asset.relativePath = FolderPathMetadata::normalizeRelativePath(query.value(7).toString());
        asset.assetType = static_cast<AssetType>(query.value(8).toInt());
        asset.sizeBytes = query.value(9).toLongLong();
        asset.modifiedAt = query.value(10).toString();
        asset.durationMs = query.value(11).toLongLong();
        const auto ffprobeSummary = buildTechnicalSummary(query.value(12).toString(), asset.durationMs, query.value(13).toLongLong());
        const auto embeddedSummary = buildEmbeddedMetadataSummary(
            query.value(16).toString(), query.value(17).toString(), query.value(18).toString(),
            query.value(19).toInt(), query.value(20).toInt(), query.value(21).toDouble(),
            query.value(22).toString(), query.value(23).toString(), query.value(24).toInt(),
            query.value(25).toInt(), query.value(26).toString());
        asset.technicalSummary = QStringList{ffprobeSummary, embeddedSummary}
                                     .filter(QRegularExpression(QStringLiteral(".+")))
                                     .join(QStringLiteral(" · "));
        asset.embeddedMetadataText = query.value(27).toString();
        asset.thumbnailPath = query.value(28).toString();
        asset.thumbnailStatus = static_cast<ThumbnailStatus>(query.value(29).toInt());
        asset.videoKey = QStringLiteral("%1:%2").arg(project.id).arg(asset.assetId);
        asset.assetKey = asset.videoKey;
        const auto parentRelativePath = FolderPathMetadata::parentRelativePath(asset.relativePath);
        asset.folderKey = FolderPathMetadata::globalFolderKey(
            project.id,
            asset.sourceRootId,
            parentRelativePath);
        const auto folderDate = FolderPathMetadata::inferDate(
            FolderPathMetadata::folderName(sourceRootPath, asset.sourceRootName),
            parentRelativePath);
        const auto embeddedCaptureTime = query.value(15).toString().trimmed();
        const auto parsedEmbeddedTime = QDateTime::fromString(embeddedCaptureTime, Qt::ISODate);
        if (!embeddedCaptureTime.isEmpty() && parsedEmbeddedTime.isValid()) {
            asset.captureTime = embeddedCaptureTime;
            asset.captureDate = parsedEmbeddedTime.date().toString(Qt::ISODate);
            asset.captureTimeSource = QStringLiteral("ExifTool");
            asset.captureTimeConfidence = 0.99;
        } else {
            const auto captureTime = captureTimeResolver.resolve(query.value(14).toString(),
                                                                 folderDate.normalizedDate,
                                                                 asset.modifiedAt);
            asset.captureTime = captureTime.captureTime;
            asset.captureDate = captureTime.captureDate;
            asset.captureTimeSource = captureTime.source;
            asset.captureTimeConfidence = captureTime.confidence;
        }
        const auto sourceKey = FolderPathMetadata::normalizedPathKey(sourceRootPath);
        if (!sourceAvailability.contains(sourceKey)) {
            sourceAvailability.insert(sourceKey, QFileInfo(sourceRootPath).isDir());
        }
        asset.available = sourceAvailability.value(sourceKey) && query.value(30).toInt() == 1;
        assets.append(asset);
    }
    return assets;
}

QVector<ProjectFolderState> fetchProjectFolders(QSqlDatabase &projectDb,
                                                const Project &project,
                                                QString *errorMessage)
{
    QVector<ProjectFolderState> folders;
    QSqlQuery query(projectDb);
    query.prepare(QStringLiteral(
        "SELECT fn.id, fn.source_root_id, COALESCE(sr.name, ''), COALESCE(sr.path, ''), "
        "COALESCE(fn.name, ''), fn.absolute_path, COALESCE(fn.path_key, ''), fn.relative_path, "
        "COALESCE(fn.parent_relative_path, ''), COALESCE(fn.depth, 0), "
        "COALESCE(fn.direct_file_count, fn.file_count, 0), COALESCE(fn.recursive_file_count, fn.file_count, 0), "
        "COALESCE(fn.normalized_date, ''), COALESCE(fn.date_anchor, '') "
        "FROM folder_node fn LEFT JOIN source_root sr ON sr.id = fn.source_root_id "
        "ORDER BY fn.source_root_id, fn.depth, fn.relative_path"));
    if (!execQuery(query, errorMessage)) {
        return folders;
    }

    QHash<QString, bool> sourceAvailability;
    while (query.next()) {
        ProjectFolderState folder;
        folder.folderId = query.value(0).toLongLong();
        folder.sourceRootId = query.value(1).toLongLong();
        folder.sourceRootName = query.value(2).toString();
        folder.sourceRootPath = query.value(3).toString();
        folder.name = query.value(4).toString();
        folder.absolutePath = query.value(5).toString();
        folder.pathKey = query.value(6).toString();
        folder.relativePath = FolderPathMetadata::normalizeRelativePath(query.value(7).toString());
        folder.parentRelativePath = FolderPathMetadata::normalizeRelativePath(query.value(8).toString());
        folder.depth = query.value(9).toInt();
        folder.directFileCount = query.value(10).toLongLong();
        folder.recursiveFileCount = query.value(11).toLongLong();
        folder.normalizedDate = query.value(12).toString();
        folder.dateAnchor = query.value(13).toString();
        folder.projectUuid = project.id;
        folder.projectName = project.name;
        folder.projectDatabasePath = project.databasePath;
        folder.folderKey = FolderPathMetadata::globalFolderKey(project.id,
                                                                folder.sourceRootId,
                                                                folder.relativePath);
        if (folder.pathKey.isEmpty()) {
            folder.pathKey = FolderPathMetadata::normalizedPathKey(folder.absolutePath);
        }
        const auto sourceKey = FolderPathMetadata::normalizedPathKey(folder.sourceRootPath);
        if (!sourceAvailability.contains(sourceKey)) {
            sourceAvailability.insert(sourceKey, QFileInfo(folder.sourceRootPath).isDir());
        }
        folder.available = sourceAvailability.value(sourceKey);
        folders.append(folder);
    }
    return folders;
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

bool markProjectOffline(QSqlDatabase &globalDb,
                        const Project &project,
                        const QString &reason,
                        QString *errorMessage)
{
    if (!globalDb.transaction()) {
        if (errorMessage) {
            *errorMessage = globalDb.lastError().text();
        }
        return false;
    }

    QString failure;
    if (!updateProjectRegistry(globalDb, project, QStringLiteral("offline"), reason, &failure)) {
        globalDb.rollback();
        if (errorMessage) {
            *errorMessage = failure;
        }
        return false;
    }

    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery folders(globalDb);
    folders.prepare(QStringLiteral(
        "UPDATE global_folder_node SET is_available = 0, last_synced_at = ?, updated_at = ? WHERE project_uuid = ?"));
    folders.addBindValue(now);
    folders.addBindValue(now);
    folders.addBindValue(project.id);
    if (!execQuery(folders, &failure)) {
        globalDb.rollback();
        if (errorMessage) {
            *errorMessage = failure;
        }
        return false;
    }

    QSqlQuery assets(globalDb);
    assets.prepare(QStringLiteral(
        "UPDATE global_video_asset SET is_available = 0, last_synced_at = ?, updated_at = ? WHERE project_uuid = ?"));
    assets.addBindValue(now);
    assets.addBindValue(now);
    assets.addBindValue(project.id);
    if (!execQuery(assets, &failure)) {
        globalDb.rollback();
        if (errorMessage) {
            *errorMessage = failure;
        }
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

    auto summary = emptyIfNull(asset.summary);
    auto keywords = asset.keywords.join(QLatin1Char(' '));
    QSqlQuery analysis(globalDb);
    analysis.prepare(QStringLiteral(
        "SELECT COALESCE(summary, ''), COALESCE(keywords_json, '') "
        "FROM video_analysis_result WHERE video_key = ?"));
    analysis.addBindValue(asset.videoKey);
    if (!execQuery(analysis, errorMessage)) {
        return false;
    }
    if (analysis.next()) {
        summary = analysis.value(0).toString();
        keywords = analysis.value(1).toString();
    }

    QString captions;
    QSqlQuery frameText(globalDb);
    frameText.prepare(QStringLiteral(
        "SELECT GROUP_CONCAT(TRIM(COALESCE(caption, '') || ' ' || COALESCE(ocr_text, '')), ' ') "
        "FROM video_frame_analysis WHERE video_key = ?"));
    frameText.addBindValue(asset.videoKey);
    if (!execQuery(frameText, errorMessage)) {
        return false;
    }
    if (frameText.next()) {
        captions = frameText.value(0).toString();
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
    query.addBindValue(QStringList{emptyIfNull(asset.technicalSummary), emptyIfNull(asset.embeddedMetadataText)}
                           .join(QLatin1Char(' ')));
    query.addBindValue(summary);
    query.addBindValue(keywords);
    query.addBindValue(captions);
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
        QStringLiteral("DELETE FROM video_analysis_plan WHERE video_key = ?"),
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
        QStringLiteral("UPDATE video_analysis_plan SET video_key = ? WHERE video_key = ?"),
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
    if (errorMessage) {
        errorMessage->clear();
    }
    const auto projectConnectionName = QStringLiteral("sync_project_%1_%2")
        .arg(project.id)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString openError;
    auto projectDb = openProjectConnection(project.databasePath, projectConnectionName, &openError);
    if (!projectDb.isOpen()) {
        QString offlineError;
        markProjectOffline(globalDb, project, openError, &offlineError);
        if (errorMessage) {
            *errorMessage = offlineError.isEmpty()
                ? openError
                : QStringLiteral("%1；更新离线状态失败：%2").arg(openError, offlineError);
        }
        projectDb = QSqlDatabase();
        closeProjectConnection(projectConnectionName);
        return false;
    }

    QString sourceStateError;
    const bool unstableSourceRoot = hasUnstableSourceRoot(projectDb, &sourceStateError);
    if (!sourceStateError.isEmpty()) {
        projectDb.close();
        projectDb = QSqlDatabase();
        closeProjectConnection(projectConnectionName);
        if (errorMessage) {
            *errorMessage = sourceStateError;
        }
        return false;
    }
    if (unstableSourceRoot) {
        projectDb.close();
        projectDb = QSqlDatabase();
        closeProjectConnection(projectConnectionName);
        return true;
    }

    QString fetchError;
    const auto folders = fetchProjectFolders(projectDb, project, &fetchError);
    const auto assets = fetchError.isEmpty()
        ? fetchProjectAssets(projectDb, project, &fetchError)
        : QVector<GlobalVideoAsset>();
    projectDb.close();
    projectDb = QSqlDatabase();
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

    QSet<QString> existingFolderKeys;
    QSqlQuery readExistingFolders(globalDb);
    readExistingFolders.prepare(QStringLiteral("SELECT folder_key FROM global_folder_node WHERE project_uuid = ?"));
    readExistingFolders.addBindValue(project.id);
    if (!execQuery(readExistingFolders, errorMessage)) {
        globalDb.rollback();
        return false;
    }
    while (readExistingFolders.next()) {
        existingFolderKeys.insert(readExistingFolders.value(0).toString());
    }

    QSet<QString> currentFolderKeys;
    QSqlQuery upsertFolder(globalDb);
    upsertFolder.prepare(QStringLiteral(
        "INSERT INTO global_folder_node "
        "(folder_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
        "source_root_path, source_root_path_key, folder_id, name, absolute_path, path_key, relative_path, "
        "parent_relative_path, depth, direct_file_count, recursive_file_count, normalized_date, date_anchor, "
        "is_available, last_synced_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(folder_key) DO UPDATE SET "
        "project_uuid = excluded.project_uuid, project_name = excluded.project_name, "
        "project_database_path = excluded.project_database_path, source_root_id = excluded.source_root_id, "
        "source_root_name = excluded.source_root_name, source_root_path = excluded.source_root_path, "
        "source_root_path_key = excluded.source_root_path_key, folder_id = excluded.folder_id, name = excluded.name, "
        "absolute_path = excluded.absolute_path, path_key = excluded.path_key, relative_path = excluded.relative_path, "
        "parent_relative_path = excluded.parent_relative_path, depth = excluded.depth, "
        "direct_file_count = excluded.direct_file_count, recursive_file_count = excluded.recursive_file_count, "
        "normalized_date = excluded.normalized_date, date_anchor = excluded.date_anchor, "
        "is_available = excluded.is_available, last_synced_at = excluded.last_synced_at, updated_at = excluded.updated_at"));
    for (const auto &folder : folders) {
        currentFolderKeys.insert(folder.folderKey);
        upsertFolder.addBindValue(folder.folderKey);
        upsertFolder.addBindValue(folder.projectUuid);
        upsertFolder.addBindValue(folder.projectName);
        upsertFolder.addBindValue(folder.projectDatabasePath);
        upsertFolder.addBindValue(folder.sourceRootId);
        upsertFolder.addBindValue(folder.sourceRootName);
        upsertFolder.addBindValue(folder.sourceRootPath);
        upsertFolder.addBindValue(FolderPathMetadata::normalizedPathKey(folder.sourceRootPath));
        upsertFolder.addBindValue(folder.folderId);
        upsertFolder.addBindValue(folder.name);
        upsertFolder.addBindValue(folder.absolutePath);
        upsertFolder.addBindValue(folder.pathKey);
        upsertFolder.addBindValue(folder.relativePath);
        upsertFolder.addBindValue(folder.parentRelativePath);
        upsertFolder.addBindValue(folder.depth);
        upsertFolder.addBindValue(folder.directFileCount);
        upsertFolder.addBindValue(folder.recursiveFileCount);
        upsertFolder.addBindValue(emptyIfNull(folder.normalizedDate));
        upsertFolder.addBindValue(emptyIfNull(folder.dateAnchor));
        upsertFolder.addBindValue(folder.available ? 1 : 0);
        upsertFolder.addBindValue(now);
        upsertFolder.addBindValue(now);
        if (!execQuery(upsertFolder, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        upsertFolder.finish();
    }

    QSet<QString> currentKeys;
    QSqlQuery clearFrames(globalDb);
    clearFrames.prepare(QStringLiteral("DELETE FROM video_frame_analysis WHERE video_key = ?"));
    QSqlQuery clearPlan(globalDb);
    clearPlan.prepare(QStringLiteral("DELETE FROM video_analysis_plan WHERE video_key = ?"));
    QSqlQuery clearResult(globalDb);
    clearResult.prepare(QStringLiteral("DELETE FROM video_analysis_result WHERE video_key = ?"));
    QSqlQuery clearTask(globalDb);
    clearTask.prepare(QStringLiteral("DELETE FROM video_analysis_task WHERE video_key = ?"));
    QSqlQuery clearDimensions(globalDb);
    clearDimensions.prepare(QStringLiteral("DELETE FROM material_dimension_analysis WHERE video_key = ?"));
    QSqlQuery clearDimensionFrames(globalDb);
    clearDimensionFrames.prepare(QStringLiteral("DELETE FROM material_dimension_frame_analysis WHERE video_key = ?"));
    QSqlQuery upsert(globalDb);
    upsert.prepare(QStringLiteral(
        "INSERT INTO global_video_asset "
        "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, folder_key, is_available, asset_id, "
        "file_name, extension, absolute_path, relative_path, asset_type, size_bytes, modified_at, "
        "capture_time, capture_date, capture_time_source, capture_time_confidence, duration_ms, "
        "thumbnail_path, thumbnail_status, analysis_status, confirmation_status, technical_summary, embedded_metadata_text, source_text, "
        "error_message, last_synced_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key) DO UPDATE SET "
        "project_uuid = excluded.project_uuid, "
        "project_name = excluded.project_name, "
        "project_database_path = excluded.project_database_path, "
        "source_root_id = excluded.source_root_id, "
        "source_root_name = excluded.source_root_name, "
        "folder_key = excluded.folder_key, "
        "is_available = excluded.is_available, "
        "asset_id = excluded.asset_id, "
        "file_name = excluded.file_name, "
        "extension = excluded.extension, "
        "absolute_path = excluded.absolute_path, "
        "relative_path = excluded.relative_path, "
        "asset_type = excluded.asset_type, "
        "size_bytes = excluded.size_bytes, "
        "modified_at = excluded.modified_at, "
        "capture_time = excluded.capture_time, "
        "capture_date = excluded.capture_date, "
        "capture_time_source = excluded.capture_time_source, "
        "capture_time_confidence = excluded.capture_time_confidence, "
        "duration_ms = excluded.duration_ms, "
        "thumbnail_path = excluded.thumbnail_path, "
        "thumbnail_status = excluded.thumbnail_status, "
        "analysis_status = excluded.analysis_status, "
        "confirmation_status = excluded.confirmation_status, "
        "technical_summary = excluded.technical_summary, "
        "embedded_metadata_text = excluded.embedded_metadata_text, "
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
        const bool newlyAnalyzable = hasExisting
            && !changed
            && existing.analysisStatus == VideoAnalysisStatus::IndexedOnly
            && canAnalyzeAsset(asset.assetType, asset.extension);
        asset.technicalSummary = emptyIfNull(asset.technicalSummary);
        asset.captureTime = emptyIfNull(asset.captureTime);
        asset.captureDate = emptyIfNull(asset.captureDate);
        asset.captureTimeSource = emptyIfNull(asset.captureTimeSource);
        asset.sourceText = changed ? QStringLiteral("") : emptyIfNull(existing.sourceText);
        const auto errorMessageText = (changed || newlyAnalyzable)
            ? QStringLiteral("")
            : emptyIfNull(existing.errorMessage);

        if (changed) {
            clearPlan.addBindValue(asset.videoKey);
            if (!execQuery(clearPlan, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            clearPlan.finish();

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

            clearTask.addBindValue(asset.videoKey);
            if (!execQuery(clearTask, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            clearTask.finish();

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
        upsert.addBindValue(asset.folderKey);
        upsert.addBindValue(asset.available ? 1 : 0);
        upsert.addBindValue(asset.assetId);
        upsert.addBindValue(asset.fileName);
        upsert.addBindValue(asset.extension);
        upsert.addBindValue(asset.absolutePath);
        upsert.addBindValue(asset.relativePath);
        upsert.addBindValue(static_cast<int>(asset.assetType));
        upsert.addBindValue(asset.sizeBytes);
        upsert.addBindValue(asset.modifiedAt);
        upsert.addBindValue(asset.captureTime);
        upsert.addBindValue(asset.captureDate);
        upsert.addBindValue(asset.captureTimeSource);
        upsert.addBindValue(asset.captureTimeConfidence);
        upsert.addBindValue(asset.durationMs);
        upsert.addBindValue(asset.thumbnailPath);
        upsert.addBindValue(static_cast<int>(asset.thumbnailStatus));
        upsert.addBindValue(static_cast<int>((changed || newlyAnalyzable)
                                                 ? initialAnalysisStatusForAsset(asset.assetType, asset.extension)
                                                 : existing.analysisStatus));
        upsert.addBindValue(static_cast<int>((changed || newlyAnalyzable)
                                                 ? ConfirmationStatus::Pending
                                                 : existing.confirmationStatus));
        upsert.addBindValue(asset.technicalSummary);
        upsert.addBindValue(asset.embeddedMetadataText);
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

        if (!upsertSearchRow(globalDb, asset, hasFts5, errorMessage)) {
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

    for (const auto &folderKey : existingFolderKeys) {
        if (currentFolderKeys.contains(folderKey)) {
            continue;
        }
        QSqlQuery deleteFolder(globalDb);
        deleteFolder.prepare(QStringLiteral("DELETE FROM global_folder_node WHERE folder_key = ? AND project_uuid = ?"));
        deleteFolder.addBindValue(folderKey);
        deleteFolder.addBindValue(project.id);
        if (!execQuery(deleteFolder, errorMessage)) {
            globalDb.rollback();
            return false;
        }
    }

    QSqlQuery cleanupFolderLinks(globalDb);
    cleanupFolderLinks.prepare(QStringLiteral(
        "UPDATE global_video_asset SET folder_key = '' WHERE project_uuid = ? AND folder_key <> '' AND NOT EXISTS ("
        "SELECT 1 FROM global_folder_node gf WHERE gf.folder_key = global_video_asset.folder_key)"));
    cleanupFolderLinks.addBindValue(project.id);
    if (!execQuery(cleanupFolderLinks, errorMessage)) {
        globalDb.rollback();
        return false;
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

MaterialCatalogSyncService::~MaterialCatalogSyncService()
{
    waitForIdle();
}

void MaterialCatalogSyncService::waitForIdle()
{
    m_futures.waitForFinished();
}

void MaterialCatalogSyncService::syncCurrentProject()
{
    if (!m_globalDatabaseManager || !m_projectService || !m_projectService->hasOpenProject()) {
        return;
    }
    syncProjectRecord(m_projectService->currentProject());
}

void MaterialCatalogSyncService::syncProject(const QString &projectDatabasePath)
{
    if (!m_globalDatabaseManager || !m_projectService || projectDatabasePath.trimmed().isEmpty()) {
        return;
    }
    Project project;
    QString errorMessage;
    if (!m_projectService->projectForPath(projectDatabasePath, &project, &errorMessage)) {
        return;
    }
    syncProjectRecord(project);
}

void MaterialCatalogSyncService::syncProjectRecord(const Project &project)
{
    if (m_syncRunning.exchange(true)) {
        if (!m_pendingProjectDatabasePaths.contains(project.databasePath)) {
            m_pendingProjectDatabasePaths.enqueue(project.databasePath);
        }
        return;
    }

    const auto jobProjectDatabasePath = m_jobEngine
        ? m_jobEngine->currentProjectDatabasePath()
        : QString();
    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::GlobalSync,
                                 QStringLiteral("同步全局索引 %1").arg(project.name),
                                 QStringLiteral("准备同步当前项目素材到素材管理中心"),
                                 0,
                                 projectJobSubject(project),
                                 projectProgressContext(QStringLiteral("同步项目索引"), 0, 1))
        : 0;

    auto future = QtConcurrent::run([this, project, jobProjectDatabasePath, jobId]() {
        const auto connectionName = QStringLiteral("global_sync_%1_%2")
            .arg(project.id)
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QString errorMessage;
        auto db = m_globalDatabaseManager->openThreadConnection(connectionName, &errorMessage);
        const auto closeConnection = [&]() {
            db.close();
            db = QSqlDatabase();
            m_globalDatabaseManager->closeThreadConnection(connectionName);
        };
        if (!db.isOpen()) {
            failJob(jobProjectDatabasePath, jobId, errorMessage);
            closeConnection();
            finishSyncRun();
            return;
        }

        updateJob(jobProjectDatabasePath, jobId, 25, QStringLiteral("正在读取项目素材索引"), projectProgressContext(QStringLiteral("同步项目索引"), 0, 1));
        if (!syncProjectIntoGlobal(db, project, m_globalDatabaseManager->hasFts5(), &errorMessage)) {
            failJob(jobProjectDatabasePath, jobId, errorMessage);
        } else {
            updateJob(jobProjectDatabasePath, jobId, 100, QStringLiteral("当前项目素材已同步到素材管理中心"), projectProgressContext(QStringLiteral("同步项目索引"), 1, 1));
            completeJob(jobProjectDatabasePath, jobId, QStringLiteral("当前项目素材已同步到素材管理中心"));
            notifyCatalogChanged();
        }
        closeConnection();
        finishSyncRun();
    });
    m_futures.addFuture(future);
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

    const auto jobProjectDatabasePath = m_jobEngine
        ? m_jobEngine->currentProjectDatabasePath()
        : QString();
    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::GlobalSync,
                                 QStringLiteral("重建全局素材索引"),
                                 QStringLiteral("准备重建所有已登记项目的素材索引"),
                                 0,
                                 catalogSubject,
                                 projectProgressContext(QStringLiteral("重建项目索引"), 0, 0))
        : 0;

    auto future = QtConcurrent::run([this, jobProjectDatabasePath, jobId]() {
        const auto connectionName = QStringLiteral("global_rebuild_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QString errorMessage;
        auto db = m_globalDatabaseManager->openThreadConnection(connectionName, &errorMessage);
        const auto closeConnection = [&]() {
            db.close();
            db = QSqlDatabase();
            m_globalDatabaseManager->closeThreadConnection(connectionName);
        };
        if (!db.isOpen()) {
            failJob(jobProjectDatabasePath, jobId, errorMessage);
            closeConnection();
            finishSyncRun();
            return;
        }

        const auto projects = loadRegisteredProjects(db, &errorMessage);
        if (!errorMessage.isEmpty()) {
            failJob(jobProjectDatabasePath, jobId, errorMessage);
            closeConnection();
            finishSyncRun();
            return;
        }
        if (projects.isEmpty()) {
            completeJob(jobProjectDatabasePath, jobId, QStringLiteral("没有可重建的已登记项目"));
            closeConnection();
            finishSyncRun();
            return;
        }

        int successCount = 0;
        int failedCount = 0;
        for (int index = 0; index < projects.size(); ++index) {
            const auto &project = projects.at(index);
            updateJob(jobProjectDatabasePath,
                      jobId,
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
                if (QFileInfo(project.databasePath).isFile()) {
                    updateProjectRegistry(db, project, QStringLiteral("failed"), syncError, nullptr);
                }
            }
        }

        if (failedCount > 0) {
            updateJob(jobProjectDatabasePath, jobId, 100, QStringLiteral("全局索引重建完成，成功 %1 个项目，失败 %2 个项目").arg(successCount).arg(failedCount), projectProgressContext(QStringLiteral("重建项目索引"), projects.size(), projects.size()));
            completeJob(jobProjectDatabasePath, jobId, QStringLiteral("全局索引重建完成，成功 %1 个项目，失败 %2 个项目").arg(successCount).arg(failedCount));
        } else {
            updateJob(jobProjectDatabasePath, jobId, 100, QStringLiteral("全局索引重建完成，共 %1 个项目").arg(successCount), projectProgressContext(QStringLiteral("重建项目索引"), projects.size(), projects.size()));
            completeJob(jobProjectDatabasePath, jobId, QStringLiteral("全局索引重建完成，共 %1 个项目").arg(successCount));
        }
        notifyCatalogChanged();
        closeConnection();
        finishSyncRun();
    });
    m_futures.addFuture(future);
}

void MaterialCatalogSyncService::updateJob(const QString &projectDatabasePath,
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

void MaterialCatalogSyncService::completeJob(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, detail]() {
        engine->completeJobForProject(projectDatabasePath, jobId, detail);
    }, Qt::QueuedConnection);
}

void MaterialCatalogSyncService::failJob(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage)
{
    if (!m_jobEngine || jobId <= 0) {
        return;
    }
    QMetaObject::invokeMethod(m_jobEngine, [engine = m_jobEngine, projectDatabasePath, jobId, errorMessage]() {
        engine->failJobForProject(projectDatabasePath, jobId, errorMessage);
    }, Qt::QueuedConnection);
}

void MaterialCatalogSyncService::finishSyncRun()
{
    QMetaObject::invokeMethod(this, [this]() {
        m_syncRunning = false;
        if (!m_pendingProjectDatabasePaths.isEmpty()) {
            const auto projectDatabasePath = m_pendingProjectDatabasePaths.dequeue();
            syncProject(projectDatabasePath);
        }
    }, Qt::QueuedConnection);
}

void MaterialCatalogSyncService::notifyCatalogChanged()
{
    QMetaObject::invokeMethod(this, [this]() {
        emit catalogChanged();
    }, Qt::QueuedConnection);
}
