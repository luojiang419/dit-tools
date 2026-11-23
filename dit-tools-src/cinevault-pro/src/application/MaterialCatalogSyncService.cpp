#include "application/MaterialCatalogSyncService.h"

#include "application/IndexingWorkCoordinator.h"

#include "application/ProjectService.h"
#include "core/jobs/JobEngine.h"
#include "core/search/CaptureTimeResolver.h"
#include "infrastructure/db/DatabaseMigration.h"
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
#include <QScopeGuard>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <functional>

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

using CatalogDeltaSink = std::function<void(const CatalogChangeSet &)>;

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
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return db;
    }
    if (!DatabaseMigration::configureSqlite(db, errorMessage)) {
        db.close();
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

QString projectAssetSelectSql(const QString &whereClause)
{
    return QStringLiteral(
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
        "%1 ORDER BY af.id").arg(whereClause);
}

QVector<GlobalVideoAsset> readProjectAssets(QSqlQuery &query, const Project &project)
{
    QVector<GlobalVideoAsset> assets;
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

QVector<GlobalVideoAsset> fetchProjectAssetPage(QSqlDatabase &projectDb,
                                                const Project &project,
                                                qint64 lastAssetId,
                                                qsizetype limit,
                                                QString *errorMessage)
{
    QSqlQuery query(projectDb);
    query.prepare(projectAssetSelectSql(QStringLiteral("WHERE af.id > ?")) + QStringLiteral(" LIMIT ?"));
    query.addBindValue(lastAssetId);
    query.addBindValue(qBound(qsizetype{1}, limit, qsizetype{1000}));
    if (!execQuery(query, errorMessage)) {
        return {};
    }
    return readProjectAssets(query, project);
}

QVector<GlobalVideoAsset> fetchProjectAssetsByIds(QSqlDatabase &projectDb,
                                                  const Project &project,
                                                  const QVector<qint64> &assetIds,
                                                  QString *errorMessage)
{
    if (assetIds.isEmpty()) {
        return {};
    }
    QStringList placeholders;
    placeholders.fill(QStringLiteral("?"), assetIds.size());
    QSqlQuery query(projectDb);
    query.prepare(projectAssetSelectSql(
        QStringLiteral("WHERE af.id IN (%1)").arg(placeholders.join(QLatin1Char(',')))));
    for (const auto assetId : assetIds) {
        query.addBindValue(assetId);
    }
    if (!execQuery(query, errorMessage)) {
        return {};
    }
    return readProjectAssets(query, project);
}

QString projectFolderSelectSql(const QString &whereClause)
{
    return QStringLiteral(
        "SELECT fn.id, fn.source_root_id, COALESCE(sr.name, ''), COALESCE(sr.path, ''), "
        "COALESCE(fn.name, ''), fn.absolute_path, COALESCE(fn.path_key, ''), fn.relative_path, "
        "COALESCE(fn.parent_relative_path, ''), COALESCE(fn.depth, 0), "
        "COALESCE(fn.direct_file_count, fn.file_count, 0), COALESCE(fn.recursive_file_count, fn.file_count, 0), "
        "COALESCE(fn.normalized_date, ''), COALESCE(fn.date_anchor, '') "
        "FROM folder_node fn LEFT JOIN source_root sr ON sr.id = fn.source_root_id "
        "%1 ORDER BY fn.id").arg(whereClause);
}

QVector<ProjectFolderState> readProjectFolders(QSqlQuery &query, const Project &project)
{
    QVector<ProjectFolderState> folders;
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

QVector<ProjectFolderState> fetchProjectFolderPage(QSqlDatabase &projectDb,
                                                   const Project &project,
                                                   qint64 lastFolderId,
                                                   qsizetype limit,
                                                   QString *errorMessage)
{
    QSqlQuery query(projectDb);
    query.prepare(projectFolderSelectSql(QStringLiteral("WHERE fn.id > ?")) + QStringLiteral(" LIMIT ?"));
    query.addBindValue(lastFolderId);
    query.addBindValue(qBound(qsizetype{1}, limit, qsizetype{1000}));
    if (!execQuery(query, errorMessage)) {
        return {};
    }
    return readProjectFolders(query, project);
}

QVector<ProjectFolderState> fetchProjectFoldersByIds(QSqlDatabase &projectDb,
                                                     const Project &project,
                                                     const QVector<qint64> &folderIds,
                                                     QString *errorMessage)
{
    if (folderIds.isEmpty()) {
        return {};
    }
    QStringList placeholders;
    placeholders.fill(QStringLiteral("?"), folderIds.size());
    QSqlQuery query(projectDb);
    query.prepare(projectFolderSelectSql(
        QStringLiteral("WHERE fn.id IN (%1)").arg(placeholders.join(QLatin1Char(',')))));
    for (const auto folderId : folderIds) {
        query.addBindValue(folderId);
    }
    if (!execQuery(query, errorMessage)) {
        return {};
    }
    return readProjectFolders(query, project);
}

bool findExistingState(QSqlDatabase &globalDb,
                       const QString &projectUuid,
                       const QString &videoKey,
                       const QString &absolutePath,
                       ExistingVideoState *state,
                       bool *found,
                       QString *errorMessage)
{
    if (found) {
        *found = false;
    }
    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral(
        "SELECT video_key, COALESCE(absolute_path, ''), size_bytes, modified_at, analysis_status, confirmation_status, "
        "COALESCE(source_text, ''), COALESCE(error_message, '') "
        "FROM global_video_asset WHERE project_uuid = ? "
        "AND (video_key = ? OR absolute_path = ? COLLATE NOCASE) "
        "ORDER BY CASE WHEN video_key = ? THEN 0 ELSE 1 END LIMIT 1"));
    query.addBindValue(projectUuid);
    query.addBindValue(videoKey);
    query.addBindValue(absolutePath);
    query.addBindValue(videoKey);
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    if (!query.next()) {
        return true;
    }
    if (state) {
        state->videoKey = query.value(0).toString();
        state->absolutePath = query.value(1).toString();
        state->sizeBytes = query.value(2).toLongLong();
        state->modifiedAt = query.value(3).toString();
        state->analysisStatus = static_cast<VideoAnalysisStatus>(query.value(4).toInt());
        state->confirmationStatus = static_cast<ConfirmationStatus>(query.value(5).toInt());
        state->sourceText = query.value(6).toString();
        state->errorMessage = query.value(7).toString();
    }
    if (found) {
        *found = true;
    }
    return true;
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

bool acquireWriterLease(IndexingWorkCoordinator *workCoordinator,
                        quint64 workGeneration,
                        IndexingWorkCoordinator::Lease *lease,
                        QString *errorMessage)
{
    if (!workCoordinator) {
        return true;
    }
    *lease = workCoordinator->acquire({
        IndexingWorkCoordinator::Resource::SqliteWriter,
        IndexingWorkCoordinator::Priority::Background,
        false,
        workGeneration});
    if (*lease) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("全局素材同步因项目切换、队列拥塞或应用退出而取消");
    }
    return false;
}

struct CatalogLogState {
    bool available = false;
    qint64 targetWatermark = 0;
    bool requiresFullRebuild = true;
};

bool readCatalogLogState(QSqlDatabase &projectDb, CatalogLogState *state, QString *errorMessage)
{
    if (state) {
        *state = {};
    }
    QSqlQuery exists(projectDb);
    exists.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'catalog_change_state'"));
    if (!execQuery(exists, errorMessage)) {
        return false;
    }
    if (!exists.next()) {
        return true;
    }

    QSqlQuery query(projectDb);
    query.prepare(QStringLiteral(
        "SELECT next_log_id, requires_full_rebuild FROM catalog_change_state WHERE singleton_id = 1"));
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    if (!query.next()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目素材变化日志状态缺失");
        }
        return false;
    }
    if (state) {
        state->available = true;
        state->targetWatermark = query.value(0).toLongLong();
        state->requiresFullRebuild = query.value(1).toInt() != 0;
    }
    return true;
}

bool readGlobalProjectState(QSqlDatabase &globalDb,
                            const QString &projectUuid,
                            qint64 *activeGeneration,
                            QString *syncStatus,
                            bool *hasIncompleteGeneration,
                            QString *errorMessage)
{
    if (activeGeneration) {
        *activeGeneration = 0;
    }
    if (syncStatus) {
        syncStatus->clear();
    }
    if (hasIncompleteGeneration) {
        *hasIncompleteGeneration = false;
    }
    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral(
        "SELECT active_sync_generation, sync_status, "
        "EXISTS(SELECT 1 FROM global_video_asset a WHERE a.project_uuid = project_registry.project_uuid "
        "AND a.sync_generation != project_registry.active_sync_generation) OR "
        "EXISTS(SELECT 1 FROM global_folder_node f WHERE f.project_uuid = project_registry.project_uuid "
        "AND f.sync_generation != project_registry.active_sync_generation) "
        "FROM project_registry WHERE project_uuid = ?"));
    query.addBindValue(projectUuid);
    if (!execQuery(query, errorMessage)) {
        return false;
    }
    if (!query.next()) {
        return true;
    }
    if (activeGeneration) {
        *activeGeneration = query.value(0).toLongLong();
    }
    if (syncStatus) {
        *syncStatus = query.value(1).toString();
    }
    if (hasIncompleteGeneration) {
        *hasIncompleteGeneration = query.value(2).toInt() != 0;
    }
    return true;
}

QVector<CatalogChange> fetchCatalogChangePage(QSqlDatabase &projectDb,
                                              qint64 afterLogId,
                                              qint64 targetWatermark,
                                              qsizetype limit,
                                              QString *errorMessage)
{
    QVector<CatalogChange> changes;
    QSqlQuery query(projectDb);
    query.prepare(QStringLiteral(
        "SELECT log_id, entity_type, entity_id, entity_key, previous_entity_key, source_root_id, "
        "previous_source_root_id, operation, change_mask FROM catalog_change_log "
        "WHERE log_id > ? AND log_id <= ? ORDER BY log_id LIMIT ?"));
    query.addBindValue(afterLogId);
    query.addBindValue(targetWatermark);
    query.addBindValue(qBound(qsizetype{1}, limit, qsizetype{500}));
    if (!execQuery(query, errorMessage)) {
        return changes;
    }
    while (query.next()) {
        CatalogChange change;
        change.logId = query.value(0).toLongLong();
        change.entity = static_cast<CatalogChangeEntity>(query.value(1).toInt());
        change.entityId = query.value(2).toLongLong();
        change.entityKey = query.value(3).toString();
        change.previousEntityKey = query.value(4).toString();
        change.sourceRootId = query.value(5).toLongLong();
        change.previousSourceRootId = query.value(6).toLongLong();
        change.operation = static_cast<CatalogChangeOperation>(query.value(7).toInt());
        change.changeMask = query.value(8).toUInt();
        changes.append(change);
    }
    return changes;
}

bool acknowledgeCatalogChanges(QSqlDatabase &projectDb,
                               qint64 throughLogId,
                               QString *errorMessage)
{
    if (!projectDb.transaction()) {
        if (errorMessage) {
            *errorMessage = projectDb.lastError().text();
        }
        return false;
    }
    QSqlQuery remove(projectDb);
    remove.prepare(QStringLiteral("DELETE FROM catalog_change_log WHERE log_id <= ?"));
    remove.addBindValue(throughLogId);
    if (!execQuery(remove, errorMessage)) {
        projectDb.rollback();
        return false;
    }
    QSqlQuery state(projectDb);
    state.prepare(QStringLiteral(
        "UPDATE catalog_change_state SET "
        "pending_change_count = (SELECT COUNT(*) FROM catalog_change_log), "
        "requires_full_rebuild = CASE WHEN (SELECT COUNT(*) FROM catalog_change_log) > 100000 "
        "THEN 1 ELSE 0 END WHERE singleton_id = 1"));
    if (!execQuery(state, errorMessage)) {
        projectDb.rollback();
        return false;
    }
    if (!projectDb.commit()) {
        if (errorMessage) {
            *errorMessage = projectDb.lastError().text();
        }
        projectDb.rollback();
        return false;
    }
    return true;
}

bool beginDeltaSync(QSqlDatabase &globalDb,
                    const Project &project,
                    IndexingWorkCoordinator *workCoordinator,
                    quint64 workGeneration,
                    QString *errorMessage)
{
    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
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
    if (!globalDb.commit()) {
        if (errorMessage) {
            *errorMessage = globalDb.lastError().text();
        }
        globalDb.rollback();
        return false;
    }
    return true;
}

bool finishDeltaSync(QSqlDatabase &globalDb,
                     const Project &project,
                     IndexingWorkCoordinator *workCoordinator,
                     quint64 workGeneration,
                     QString *errorMessage)
{
    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
    }
    QSqlQuery finish(globalDb);
    finish.prepare(QStringLiteral(
        "UPDATE project_registry SET project_name = ?, project_database_path = ?, last_synced_at = ?, "
        "sync_status = 'ok', error_message = '' WHERE project_uuid = ?"));
    finish.addBindValue(project.name);
    finish.addBindValue(project.databasePath);
    finish.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    finish.addBindValue(project.id);
    return execQuery(finish, errorMessage);
}

bool beginSyncGeneration(QSqlDatabase &globalDb,
                         const Project &project,
                         IndexingWorkCoordinator *workCoordinator,
                         quint64 workGeneration,
                         qint64 *syncGeneration,
                         QString *errorMessage)
{
    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
    }
    if (!globalDb.transaction()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        return false;
    }
    if (!updateProjectRegistry(
            globalDb, project, QStringLiteral("syncing"), QString(), errorMessage)) {
        globalDb.rollback();
        return false;
    }
    QSqlQuery generation(globalDb);
    generation.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(value), 0) + 1 FROM ("
        "SELECT active_sync_generation AS value FROM project_registry WHERE project_uuid = ? "
        "UNION ALL SELECT COALESCE(MAX(sync_generation), 0) FROM global_video_asset WHERE project_uuid = ? "
        "UNION ALL SELECT COALESCE(MAX(sync_generation), 0) FROM global_folder_node WHERE project_uuid = ?)"));
    generation.addBindValue(project.id);
    generation.addBindValue(project.id);
    generation.addBindValue(project.id);
    if (!execQuery(generation, errorMessage) || !generation.next()) {
        globalDb.rollback();
        return false;
    }
    *syncGeneration = qMax<qint64>(1, generation.value(0).toLongLong());
    if (!globalDb.commit()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        globalDb.rollback();
        return false;
    }
    return true;
}

void markSyncFailed(QSqlDatabase &globalDb,
                    const Project &project,
                    const QString &failure,
                    IndexingWorkCoordinator *workCoordinator,
                    quint64 workGeneration)
{
    globalDb.rollback();
    IndexingWorkCoordinator::Lease writerLease;
    QString ignored;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, &ignored)) {
        return;
    }
    QSqlQuery query(globalDb);
    query.prepare(QStringLiteral(
        "UPDATE project_registry SET sync_status = 'failed', error_message = ?, last_synced_at = ? "
        "WHERE project_uuid = ?"));
    query.addBindValue(failure);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(project.id);
    query.exec();
}

bool persistFolderPage(QSqlDatabase &globalDb,
                       const QVector<ProjectFolderState> &folders,
                       qint64 syncGeneration,
                       IndexingWorkCoordinator *workCoordinator,
                       quint64 workGeneration,
                       QString *errorMessage)
{
    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
    }
    if (!globalDb.transaction()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        return false;
    }
    QSqlQuery upsert(globalDb);
    upsert.prepare(QStringLiteral(
        "INSERT INTO global_folder_node "
        "(folder_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
        "source_root_path, source_root_path_key, folder_id, name, absolute_path, path_key, relative_path, "
        "parent_relative_path, depth, direct_file_count, recursive_file_count, normalized_date, date_anchor, "
        "is_available, sync_generation, last_synced_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(folder_key) DO UPDATE SET "
        "project_uuid = excluded.project_uuid, project_name = excluded.project_name, "
        "project_database_path = excluded.project_database_path, source_root_id = excluded.source_root_id, "
        "source_root_name = excluded.source_root_name, source_root_path = excluded.source_root_path, "
        "source_root_path_key = excluded.source_root_path_key, folder_id = excluded.folder_id, "
        "name = excluded.name, absolute_path = excluded.absolute_path, path_key = excluded.path_key, "
        "relative_path = excluded.relative_path, parent_relative_path = excluded.parent_relative_path, "
        "depth = excluded.depth, direct_file_count = excluded.direct_file_count, "
        "recursive_file_count = excluded.recursive_file_count, normalized_date = excluded.normalized_date, "
        "date_anchor = excluded.date_anchor, is_available = excluded.is_available, "
        "sync_generation = excluded.sync_generation, last_synced_at = excluded.last_synced_at, "
        "updated_at = excluded.updated_at"));
    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const auto &folder : folders) {
        upsert.addBindValue(folder.folderKey);
        upsert.addBindValue(folder.projectUuid);
        upsert.addBindValue(folder.projectName);
        upsert.addBindValue(folder.projectDatabasePath);
        upsert.addBindValue(folder.sourceRootId);
        upsert.addBindValue(folder.sourceRootName);
        upsert.addBindValue(folder.sourceRootPath);
        upsert.addBindValue(FolderPathMetadata::normalizedPathKey(folder.sourceRootPath));
        upsert.addBindValue(folder.folderId);
        upsert.addBindValue(folder.name);
        upsert.addBindValue(folder.absolutePath);
        upsert.addBindValue(folder.pathKey);
        upsert.addBindValue(folder.relativePath);
        upsert.addBindValue(folder.parentRelativePath);
        upsert.addBindValue(folder.depth);
        upsert.addBindValue(folder.directFileCount);
        upsert.addBindValue(folder.recursiveFileCount);
        upsert.addBindValue(emptyIfNull(folder.normalizedDate));
        upsert.addBindValue(emptyIfNull(folder.dateAnchor));
        upsert.addBindValue(folder.available ? 1 : 0);
        upsert.addBindValue(syncGeneration);
        upsert.addBindValue(now);
        upsert.addBindValue(now);
        if (!execQuery(upsert, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        upsert.finish();
    }
    if (!globalDb.commit()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        globalDb.rollback();
        return false;
    }
    return true;
}

bool clearAnalysisArtifacts(QSqlDatabase &globalDb,
                            const QString &videoKey,
                            bool hasFts5,
                            QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("DELETE FROM video_analysis_plan WHERE video_key = ?"),
        QStringLiteral("DELETE FROM video_frame_analysis WHERE video_key = ?"),
        QStringLiteral("DELETE FROM video_analysis_result WHERE video_key = ?"),
        QStringLiteral("DELETE FROM video_analysis_task WHERE video_key = ?"),
        QStringLiteral("DELETE FROM material_dimension_analysis WHERE video_key = ?"),
        QStringLiteral("DELETE FROM material_dimension_frame_analysis WHERE video_key = ?")
    };
    for (const auto &statement : statements) {
        QSqlQuery query(globalDb);
        query.prepare(statement);
        query.addBindValue(videoKey);
        if (!execQuery(query, errorMessage)) {
            return false;
        }
    }
    return deleteFtsRow(globalDb, videoKey, hasFts5, errorMessage);
}

bool persistAssetPage(QSqlDatabase &globalDb,
                      const Project &project,
                      QVector<GlobalVideoAsset> assets,
                      qint64 syncGeneration,
                      bool hasFts5,
                      IndexingWorkCoordinator *workCoordinator,
                      quint64 workGeneration,
                      QString *errorMessage)
{
    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
    }
    if (!globalDb.transaction()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        return false;
    }
    QSqlQuery upsert(globalDb);
    upsert.prepare(QStringLiteral(
        "INSERT INTO global_video_asset "
        "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
        "folder_key, is_available, asset_id, file_name, extension, absolute_path, relative_path, asset_type, "
        "size_bytes, modified_at, capture_time, capture_date, capture_time_source, capture_time_confidence, "
        "duration_ms, thumbnail_path, thumbnail_status, analysis_status, confirmation_status, "
        "technical_summary, embedded_metadata_text, source_text, error_message, sync_generation, "
        "last_synced_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(video_key) DO UPDATE SET "
        "project_uuid = excluded.project_uuid, project_name = excluded.project_name, "
        "project_database_path = excluded.project_database_path, source_root_id = excluded.source_root_id, "
        "source_root_name = excluded.source_root_name, folder_key = excluded.folder_key, "
        "is_available = excluded.is_available, asset_id = excluded.asset_id, file_name = excluded.file_name, "
        "extension = excluded.extension, absolute_path = excluded.absolute_path, "
        "relative_path = excluded.relative_path, asset_type = excluded.asset_type, "
        "analysis_status = CASE "
        "WHEN global_video_asset.size_bytes != excluded.size_bytes "
        "OR global_video_asset.modified_at != excluded.modified_at THEN excluded.analysis_status "
        "WHEN global_video_asset.analysis_status = ? AND excluded.analysis_status != ? "
        "THEN excluded.analysis_status ELSE global_video_asset.analysis_status END, "
        "confirmation_status = CASE "
        "WHEN global_video_asset.size_bytes != excluded.size_bytes "
        "OR global_video_asset.modified_at != excluded.modified_at "
        "OR (global_video_asset.analysis_status = ? AND excluded.analysis_status != ?) "
        "THEN excluded.confirmation_status ELSE global_video_asset.confirmation_status END, "
        "source_text = CASE WHEN global_video_asset.size_bytes != excluded.size_bytes "
        "OR global_video_asset.modified_at != excluded.modified_at "
        "THEN excluded.source_text ELSE global_video_asset.source_text END, "
        "error_message = CASE WHEN global_video_asset.size_bytes != excluded.size_bytes "
        "OR global_video_asset.modified_at != excluded.modified_at "
        "OR (global_video_asset.analysis_status = ? AND excluded.analysis_status != ?) "
        "THEN excluded.error_message ELSE global_video_asset.error_message END, "
        "size_bytes = excluded.size_bytes, modified_at = excluded.modified_at, "
        "capture_time = excluded.capture_time, capture_date = excluded.capture_date, "
        "capture_time_source = excluded.capture_time_source, "
        "capture_time_confidence = excluded.capture_time_confidence, duration_ms = excluded.duration_ms, "
        "thumbnail_path = excluded.thumbnail_path, thumbnail_status = excluded.thumbnail_status, "
        "technical_summary = excluded.technical_summary, "
        "embedded_metadata_text = excluded.embedded_metadata_text, "
        "sync_generation = excluded.sync_generation, last_synced_at = excluded.last_synced_at, "
        "updated_at = excluded.updated_at"));
    const auto indexedOnly = static_cast<int>(VideoAnalysisStatus::IndexedOnly);
    const auto now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (auto &asset : assets) {
        ExistingVideoState existing;
        bool hasExisting = false;
        if (!findExistingState(globalDb,
                               project.id,
                               asset.videoKey,
                               asset.absolutePath,
                               &existing,
                               &hasExisting,
                               errorMessage)) {
            globalDb.rollback();
            return false;
        }
        const bool remappedExisting = hasExisting && existing.videoKey != asset.videoKey;
        const bool changed = !hasExisting
            || existing.sizeBytes != asset.sizeBytes
            || existing.modifiedAt != asset.modifiedAt;
        const bool newlyAnalyzable = hasExisting && !changed
            && existing.analysisStatus == VideoAnalysisStatus::IndexedOnly
            && canAnalyzeAsset(asset.assetType, asset.extension);
        const auto initialStatus = initialAnalysisStatusForAsset(asset.assetType, asset.extension);
        const auto analysisStatus = remappedExisting && !changed && !newlyAnalyzable
            ? existing.analysisStatus
            : initialStatus;
        const auto confirmationStatus = remappedExisting && !changed && !newlyAnalyzable
            ? existing.confirmationStatus
            : ConfirmationStatus::Pending;
        asset.technicalSummary = emptyIfNull(asset.technicalSummary);
        asset.captureTime = emptyIfNull(asset.captureTime);
        asset.captureDate = emptyIfNull(asset.captureDate);
        asset.captureTimeSource = emptyIfNull(asset.captureTimeSource);
        asset.sourceText = changed ? QStringLiteral("") : emptyIfNull(existing.sourceText);
        const auto errorMessageText = (changed || newlyAnalyzable)
            ? QStringLiteral("")
            : emptyIfNull(existing.errorMessage);

        if ((changed || newlyAnalyzable)
            && !clearAnalysisArtifacts(globalDb, asset.videoKey, hasFts5, errorMessage)) {
            globalDb.rollback();
            return false;
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
        upsert.addBindValue(static_cast<int>(analysisStatus));
        upsert.addBindValue(static_cast<int>(confirmationStatus));
        upsert.addBindValue(asset.technicalSummary);
        upsert.addBindValue(asset.embeddedMetadataText);
        upsert.addBindValue(asset.sourceText);
        upsert.addBindValue(errorMessageText);
        upsert.addBindValue(syncGeneration);
        upsert.addBindValue(now);
        upsert.addBindValue(now);
        upsert.addBindValue(indexedOnly);
        upsert.addBindValue(indexedOnly);
        upsert.addBindValue(indexedOnly);
        upsert.addBindValue(indexedOnly);
        upsert.addBindValue(indexedOnly);
        upsert.addBindValue(indexedOnly);
        if (!execQuery(upsert, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        upsert.finish();

        if (remappedExisting && !changed && !newlyAnalyzable
            && !migrateAnalysisArtifacts(
                globalDb, existing.videoKey, asset.videoKey, hasFts5, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        if (!upsertSearchRow(globalDb, asset, hasFts5, errorMessage)) {
            globalDb.rollback();
            return false;
        }
    }
    if (!globalDb.commit()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        globalDb.rollback();
        return false;
    }
    return true;
}

bool applyDeltaCleanup(QSqlDatabase &globalDb,
                       const Project &project,
                       QVector<CatalogChange> *changes,
                       const QVector<GlobalVideoAsset> &assets,
                       const QVector<ProjectFolderState> &folders,
                       bool hasFts5,
                       IndexingWorkCoordinator *workCoordinator,
                       quint64 workGeneration,
                       QString *errorMessage)
{
    QSet<qint64> existingAssetIds;
    for (const auto &asset : assets) {
        existingAssetIds.insert(asset.assetId);
    }
    QHash<qint64, QString> currentFolderKeys;
    for (const auto &folder : folders) {
        currentFolderKeys.insert(folder.folderId, folder.folderKey);
    }

    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
    }
    if (!globalDb.transaction()) {
        if (errorMessage) {
            *errorMessage = globalDb.lastError().text();
        }
        return false;
    }

    for (auto &change : *changes) {
        if (change.entity == CatalogChangeEntity::Asset) {
            if (existingAssetIds.contains(change.entityId)) {
                continue;
            }
            change.operation = CatalogChangeOperation::Removed;
            const auto videoKey = QStringLiteral("%1:%2").arg(project.id).arg(change.entityId);
            if (!clearAnalysisArtifacts(globalDb, videoKey, hasFts5, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            QSqlQuery remove(globalDb);
            remove.prepare(QStringLiteral(
                "DELETE FROM global_video_asset WHERE project_uuid = ? AND asset_id = ?"));
            remove.addBindValue(project.id);
            remove.addBindValue(change.entityId);
            if (!execQuery(remove, errorMessage)) {
                globalDb.rollback();
                return false;
            }
            continue;
        }

        const auto hasCurrentFolder = currentFolderKeys.contains(change.entityId);
        const auto currentFolderKey = currentFolderKeys.value(change.entityId);
        auto oldRelativePath = change.previousEntityKey;
        auto oldSourceRootId = change.previousSourceRootId;
        if (!hasCurrentFolder) {
            change.operation = CatalogChangeOperation::Removed;
            oldRelativePath = change.entityKey;
            oldSourceRootId = change.sourceRootId;
        }
        const auto oldFolderKey = oldRelativePath.isEmpty() && oldSourceRootId == 0
            ? QString()
            : FolderPathMetadata::globalFolderKey(project.id, oldSourceRootId, oldRelativePath);
        if (oldFolderKey.isEmpty() || oldFolderKey == currentFolderKey) {
            continue;
        }

        QSqlQuery relink(globalDb);
        relink.prepare(QStringLiteral(
            "UPDATE global_video_asset SET folder_key = ? WHERE project_uuid = ? AND folder_key = ?"));
        relink.addBindValue(hasCurrentFolder ? currentFolderKey : QString());
        relink.addBindValue(project.id);
        relink.addBindValue(oldFolderKey);
        if (!execQuery(relink, errorMessage)) {
            globalDb.rollback();
            return false;
        }
        QSqlQuery removeFolder(globalDb);
        removeFolder.prepare(QStringLiteral(
            "DELETE FROM global_folder_node WHERE project_uuid = ? AND folder_key = ?"));
        removeFolder.addBindValue(project.id);
        removeFolder.addBindValue(oldFolderKey);
        if (!execQuery(removeFolder, errorMessage)) {
            globalDb.rollback();
            return false;
        }
    }

    QSqlQuery cleanupFolderLinks(globalDb);
    cleanupFolderLinks.prepare(QStringLiteral(
        "UPDATE global_video_asset SET folder_key = '' WHERE project_uuid = ? AND folder_key <> '' "
        "AND NOT EXISTS (SELECT 1 FROM global_folder_node gf "
        "WHERE gf.folder_key = global_video_asset.folder_key)"));
    cleanupFolderLinks.addBindValue(project.id);
    if (!execQuery(cleanupFolderLinks, errorMessage)) {
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

bool finishSyncGeneration(QSqlDatabase &globalDb,
                          const Project &project,
                          qint64 syncGeneration,
                          bool hasFts5,
                          IndexingWorkCoordinator *workCoordinator,
                          quint64 workGeneration,
                          QString *errorMessage)
{
    IndexingWorkCoordinator::Lease writerLease;
    if (!acquireWriterLease(workCoordinator, workGeneration, &writerLease, errorMessage)) {
        return false;
    }
    if (!globalDb.transaction()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        return false;
    }
    if (hasFts5) {
        QSqlQuery deleteFts(globalDb);
        deleteFts.prepare(QStringLiteral(
            "DELETE FROM video_search_fts WHERE video_key IN ("
            "SELECT video_key FROM global_video_asset WHERE project_uuid = ? AND sync_generation != ?)"));
        deleteFts.addBindValue(project.id);
        deleteFts.addBindValue(syncGeneration);
        if (!execQuery(deleteFts, errorMessage)) {
            globalDb.rollback();
            return false;
        }
    }
    QSqlQuery deleteAssets(globalDb);
    deleteAssets.prepare(QStringLiteral(
        "DELETE FROM global_video_asset WHERE project_uuid = ? AND sync_generation != ?"));
    deleteAssets.addBindValue(project.id);
    deleteAssets.addBindValue(syncGeneration);
    if (!execQuery(deleteAssets, errorMessage)) {
        globalDb.rollback();
        return false;
    }
    QSqlQuery deleteFolders(globalDb);
    deleteFolders.prepare(QStringLiteral(
        "DELETE FROM global_folder_node WHERE project_uuid = ? AND sync_generation != ?"));
    deleteFolders.addBindValue(project.id);
    deleteFolders.addBindValue(syncGeneration);
    if (!execQuery(deleteFolders, errorMessage)) {
        globalDb.rollback();
        return false;
    }
    QSqlQuery cleanupFolderLinks(globalDb);
    cleanupFolderLinks.prepare(QStringLiteral(
        "UPDATE global_video_asset SET folder_key = '' WHERE project_uuid = ? AND folder_key <> '' "
        "AND NOT EXISTS (SELECT 1 FROM global_folder_node gf "
        "WHERE gf.folder_key = global_video_asset.folder_key)"));
    cleanupFolderLinks.addBindValue(project.id);
    if (!execQuery(cleanupFolderLinks, errorMessage)) {
        globalDb.rollback();
        return false;
    }
    QSqlQuery finish(globalDb);
    finish.prepare(QStringLiteral(
        "UPDATE project_registry SET project_name = ?, project_database_path = ?, last_synced_at = ?, "
        "sync_status = 'ok', active_sync_generation = ?, error_message = '' WHERE project_uuid = ?"));
    finish.addBindValue(project.name);
    finish.addBindValue(project.databasePath);
    finish.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    finish.addBindValue(syncGeneration);
    finish.addBindValue(project.id);
    if (!execQuery(finish, errorMessage)) {
        globalDb.rollback();
        return false;
    }
    if (!globalDb.commit()) {
        if (errorMessage) *errorMessage = globalDb.lastError().text();
        globalDb.rollback();
        return false;
    }
    return true;
}

bool syncProjectIntoGlobalFull(QSqlDatabase &globalDb,
                               const Project &project,
                               bool hasFts5,
                               QString *errorMessage,
                               IndexingWorkCoordinator *workCoordinator,
                               quint64 workGeneration,
                               const CatalogDeltaSink &deltaSink)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const auto connectionName = QStringLiteral("sync_project_full_%1_%2")
        .arg(project.id)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString openError;
    auto projectDb = openProjectConnection(project.databasePath, connectionName, &openError);
    if (!projectDb.isOpen()) {
        QString offlineError;
        markProjectOffline(globalDb, project, openError, &offlineError);
        if (errorMessage) {
            *errorMessage = offlineError.isEmpty()
                ? openError
                : QStringLiteral("%1；更新离线状态失败：%2").arg(openError, offlineError);
        }
        projectDb = QSqlDatabase();
        closeProjectConnection(connectionName);
        return false;
    }
    const auto closeGuard = qScopeGuard([&]() {
        projectDb.close();
        projectDb = QSqlDatabase();
        closeProjectConnection(connectionName);
    });

    QString sourceStateError;
    const bool unstableSourceRoot = hasUnstableSourceRoot(projectDb, &sourceStateError);
    if (!sourceStateError.isEmpty()) {
        if (errorMessage) *errorMessage = sourceStateError;
        return false;
    }
    if (unstableSourceRoot) {
        return true;
    }

    CatalogLogState catalogLogState;
    if (!readCatalogLogState(projectDb, &catalogLogState, errorMessage)) {
        return false;
    }

    qint64 syncGeneration = 0;
    if (!beginSyncGeneration(globalDb,
                             project,
                             workCoordinator,
                             workGeneration,
                             &syncGeneration,
                             errorMessage)) {
        return false;
    }
    bool completed = false;
    const auto failureGuard = qScopeGuard([&]() {
        if (!completed) {
            markSyncFailed(globalDb,
                           project,
                           errorMessage ? *errorMessage : QStringLiteral("全局素材同步失败"),
                           workCoordinator,
                           workGeneration);
        }
    });

    constexpr qsizetype PageSize = 500;
    qint64 lastFolderId = 0;
    while (true) {
        auto folders = fetchProjectFolderPage(
            projectDb, project, lastFolderId, PageSize, errorMessage);
        if (errorMessage && !errorMessage->isEmpty()) {
            return false;
        }
        if (folders.isEmpty()) {
            break;
        }
        lastFolderId = folders.constLast().folderId;
        if (!persistFolderPage(globalDb,
                               folders,
                               syncGeneration,
                               workCoordinator,
                               workGeneration,
                               errorMessage)) {
            return false;
        }
    }

    qint64 lastAssetId = 0;
    while (true) {
        auto assets = fetchProjectAssetPage(
            projectDb, project, lastAssetId, PageSize, errorMessage);
        if (errorMessage && !errorMessage->isEmpty()) {
            return false;
        }
        if (assets.isEmpty()) {
            break;
        }
        lastAssetId = assets.constLast().assetId;
        if (!persistAssetPage(globalDb,
                              project,
                              std::move(assets),
                              syncGeneration,
                              hasFts5,
                              workCoordinator,
                              workGeneration,
                              errorMessage)) {
            return false;
        }
    }

    if (!finishSyncGeneration(globalDb,
                              project,
                              syncGeneration,
                              hasFts5,
                              workCoordinator,
                              workGeneration,
                              errorMessage)) {
        return false;
    }
    if (catalogLogState.available
        && !acknowledgeCatalogChanges(projectDb, catalogLogState.targetWatermark, errorMessage)) {
        return false;
    }
    if (deltaSink) {
        CatalogChangeSet changeSet;
        changeSet.projectUuid = project.id;
        changeSet.throughLogId = catalogLogState.targetWatermark;
        changeSet.fullRebuild = true;
        deltaSink(changeSet);
    }
    completed = true;
    return true;
}

bool syncProjectIntoGlobalDelta(QSqlDatabase &globalDb,
                                QSqlDatabase &projectDb,
                                const Project &project,
                                qint64 activeGeneration,
                                qint64 targetWatermark,
                                bool hasFts5,
                                QString *errorMessage,
                                IndexingWorkCoordinator *workCoordinator,
                                quint64 workGeneration,
                                const CatalogDeltaSink &deltaSink)
{
    if (!beginDeltaSync(globalDb, project, workCoordinator, workGeneration, errorMessage)) {
        return false;
    }
    bool completed = false;
    const auto failureGuard = qScopeGuard([&]() {
        if (!completed) {
            markSyncFailed(globalDb,
                           project,
                           errorMessage ? *errorMessage : QStringLiteral("全局素材增量同步失败"),
                           workCoordinator,
                           workGeneration);
        }
    });

    constexpr qsizetype PageSize = 500;
    qint64 lastLogId = 0;
    while (lastLogId < targetWatermark) {
        auto changes = fetchCatalogChangePage(
            projectDb, lastLogId, targetWatermark, PageSize, errorMessage);
        if (errorMessage && !errorMessage->isEmpty()) {
            return false;
        }
        if (changes.isEmpty()) {
            break;
        }

        QVector<qint64> assetIds;
        QVector<qint64> folderIds;
        assetIds.reserve(changes.size());
        folderIds.reserve(changes.size());
        for (const auto &change : changes) {
            if (change.entity == CatalogChangeEntity::Asset) {
                assetIds.append(change.entityId);
            } else if (change.entity == CatalogChangeEntity::Folder) {
                folderIds.append(change.entityId);
            }
        }
        const auto assets = fetchProjectAssetsByIds(projectDb, project, assetIds, errorMessage);
        if (errorMessage && !errorMessage->isEmpty()) {
            return false;
        }
        const auto folders = fetchProjectFoldersByIds(projectDb, project, folderIds, errorMessage);
        if (errorMessage && !errorMessage->isEmpty()) {
            return false;
        }
        if (!folders.isEmpty()
            && !persistFolderPage(globalDb,
                                  folders,
                                  activeGeneration,
                                  workCoordinator,
                                  workGeneration,
                                  errorMessage)) {
            return false;
        }
        if (!assets.isEmpty()
            && !persistAssetPage(globalDb,
                                 project,
                                 assets,
                                 activeGeneration,
                                 hasFts5,
                                 workCoordinator,
                                 workGeneration,
                                 errorMessage)) {
            return false;
        }
        if (!applyDeltaCleanup(globalDb,
                               project,
                               &changes,
                               assets,
                               folders,
                               hasFts5,
                               workCoordinator,
                               workGeneration,
                               errorMessage)) {
            return false;
        }

        lastLogId = changes.constLast().logId;
        if (deltaSink) {
            CatalogChangeSet changeSet;
            changeSet.projectUuid = project.id;
            changeSet.changes = changes;
            changeSet.throughLogId = lastLogId;
            deltaSink(changeSet);
        }
    }

    if (!finishDeltaSync(globalDb, project, workCoordinator, workGeneration, errorMessage)) {
        return false;
    }
    if (!acknowledgeCatalogChanges(projectDb, targetWatermark, errorMessage)) {
        return false;
    }
    completed = true;
    return true;
}

bool syncProjectIntoGlobal(QSqlDatabase &globalDb,
                           const Project &project,
                           bool hasFts5,
                           bool forceFullRebuild,
                           QString *errorMessage,
                           IndexingWorkCoordinator *workCoordinator = nullptr,
                           quint64 workGeneration = 0,
                           const CatalogDeltaSink &deltaSink = {})
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const auto connectionName = QStringLiteral("sync_project_dispatch_%1_%2")
        .arg(project.id)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QString openError;
    auto projectDb = openProjectConnection(project.databasePath, connectionName, &openError);
    if (!projectDb.isOpen()) {
        QString offlineError;
        markProjectOffline(globalDb, project, openError, &offlineError);
        if (errorMessage) {
            *errorMessage = offlineError.isEmpty()
                ? openError
                : QStringLiteral("%1；更新离线状态失败：%2").arg(openError, offlineError);
        }
        projectDb = QSqlDatabase();
        closeProjectConnection(connectionName);
        return false;
    }
    const auto closeGuard = qScopeGuard([&]() {
        projectDb.close();
        projectDb = QSqlDatabase();
        closeProjectConnection(connectionName);
    });

    QString sourceStateError;
    const bool unstableSourceRoot = hasUnstableSourceRoot(projectDb, &sourceStateError);
    if (!sourceStateError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = sourceStateError;
        }
        return false;
    }
    if (unstableSourceRoot) {
        return true;
    }

    CatalogLogState catalogLogState;
    if (!readCatalogLogState(projectDb, &catalogLogState, errorMessage)) {
        return false;
    }
    qint64 activeGeneration = 0;
    QString syncStatus;
    bool hasIncompleteGeneration = false;
    if (!readGlobalProjectState(globalDb,
                                project.id,
                                &activeGeneration,
                                &syncStatus,
                                &hasIncompleteGeneration,
                                errorMessage)) {
        return false;
    }
    const bool needsFullRebuild = forceFullRebuild
        || !catalogLogState.available
        || catalogLogState.requiresFullRebuild
        || activeGeneration <= 0
        || hasIncompleteGeneration
        || syncStatus.compare(QStringLiteral("offline"), Qt::CaseInsensitive) == 0;
    if (needsFullRebuild) {
        return syncProjectIntoGlobalFull(globalDb,
                                         project,
                                         hasFts5,
                                         errorMessage,
                                         workCoordinator,
                                         workGeneration,
                                         deltaSink);
    }
    return syncProjectIntoGlobalDelta(globalDb,
                                      projectDb,
                                      project,
                                      activeGeneration,
                                      catalogLogState.targetWatermark,
                                      hasFts5,
                                      errorMessage,
                                      workCoordinator,
                                      workGeneration,
                                      deltaSink);
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
    return syncProjectIntoGlobal(globalDb, project, hasFts5, false, errorMessage);
}

bool syncProjectIntoGlobalWithDeltasForTest(QSqlDatabase &globalDb,
                                            const Project &project,
                                            bool hasFts5,
                                            QVector<CatalogChangeSet> *changeSets,
                                            QString *errorMessage)
{
    return syncProjectIntoGlobal(
        globalDb,
        project,
        hasFts5,
        false,
        errorMessage,
        nullptr,
        0,
        [changeSets](const CatalogChangeSet &changeSet) {
            if (changeSets) {
                changeSets->append(changeSet);
            }
        });
}

bool rebuildProjectIntoGlobalForTest(QSqlDatabase &globalDb,
                                     const Project &project,
                                     bool hasFts5,
                                     QString *errorMessage)
{
    return syncProjectIntoGlobal(globalDb, project, hasFts5, true, errorMessage);
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

void MaterialCatalogSyncService::setWorkCoordinator(IndexingWorkCoordinator *workCoordinator)
{
    m_workCoordinator = workCoordinator;
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

    const auto workGeneration = m_workCoordinator
        ? m_workCoordinator->currentGeneration()
        : quint64{0};
    auto future = QtConcurrent::run([this, project, jobProjectDatabasePath, jobId, workGeneration]() {
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
        if (!syncProjectIntoGlobal(db,
                                   project,
                                   m_globalDatabaseManager->hasFts5(),
                                   false,
                                   &errorMessage,
                                   m_workCoordinator,
                                   workGeneration,
                                   [this](const CatalogChangeSet &changeSet) {
                                       notifyCatalogDelta(changeSet);
                                   })) {
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

    const auto workGeneration = m_workCoordinator
        ? m_workCoordinator->currentGeneration()
        : quint64{0};
    auto future = QtConcurrent::run([this, jobProjectDatabasePath, jobId, workGeneration]() {
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
            if (syncProjectIntoGlobal(db,
                                      project,
                                      m_globalDatabaseManager->hasFts5(),
                                      true,
                                      &syncError,
                                      m_workCoordinator,
                                      workGeneration,
                                      [this](const CatalogChangeSet &changeSet) {
                                          notifyCatalogDelta(changeSet);
                                      })) {
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

void MaterialCatalogSyncService::notifyCatalogDelta(const CatalogChangeSet &changeSet)
{
    QMetaObject::invokeMethod(this, [this, changeSet]() {
        emit catalogDeltaReady(changeSet);
    }, Qt::QueuedConnection);
}
