#include "application/ReportExportService.h"

#include "application/ProjectService.h"
#include "core/report/ReportRenderEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "shared/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include <algorithm>

namespace {
QString safeFileName(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        value = QStringLiteral("未命名项目");
    }
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    return value;
}

QString normalizedRelativePath(QString value)
{
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (value.startsWith(QLatin1String("./"))) {
        value.remove(0, 2);
    }
    return value;
}

bool execOrError(QSqlQuery &query, const QString &context, QString *errorMessage)
{
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("%1失败：%2").arg(context, query.lastError().text());
    }
    return false;
}
}

ReportExportService::ReportExportService(DatabaseManager *databaseManager, ProjectService *projectService, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_projectService(projectService)
{
}

bool ReportExportService::hasOpenProject() const
{
    return m_projectService && m_projectService->hasOpenProject() && m_databaseManager && m_databaseManager->hasOpenProject();
}

QString ReportExportService::defaultReportPath() const
{
    if (!m_projectService || !m_projectService->hasOpenProject()) {
        return {};
    }

    const auto project = m_projectService->currentProject();
    const auto fileName = QStringLiteral("%1_DIT报表_%2.pdf")
        .arg(safeFileName(project.name),
             QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    return QDir(project.rootPath).filePath(QStringLiteral("reports/%1").arg(fileName));
}

bool ReportExportService::exportPdf(const ReportExportRequest &request, QString *outputPath, QString *errorMessage) const
{
    if (!hasOpenProject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先创建或打开项目。");
        }
        return false;
    }

    auto resolvedRequest = request;
    if (resolvedRequest.outputPath.trimmed().isEmpty()) {
        resolvedRequest.outputPath = defaultReportPath();
    }
    if (!resolvedRequest.outputPath.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        resolvedRequest.outputPath += QStringLiteral(".pdf");
    }

    const QFileInfo outputInfo(resolvedRequest.outputPath);
    QDir dir;
    if (!dir.mkpath(outputInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建报表输出目录：%1").arg(outputInfo.absolutePath());
        }
        return false;
    }

    ReportDocument document;
    if (!buildDocument(resolvedRequest, &document, errorMessage)) {
        return false;
    }

    ReportRenderEngine renderer;
    if (!renderer.renderPdf(document, resolvedRequest.outputPath, errorMessage)) {
        return false;
    }

    if (outputPath) {
        *outputPath = resolvedRequest.outputPath;
    }
    return true;
}

bool ReportExportService::generatePreview(const ReportExportRequest &request, QStringList *pagePaths, QString *errorMessage) const
{
    if (!hasOpenProject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先创建或打开项目。");
        }
        return false;
    }

    ReportDocument document;
    if (!buildDocument(request, &document, errorMessage)) {
        return false;
    }

    const auto project = m_projectService->currentProject();
    const auto previewRoot = Paths::projectReportPreviewRoot(project.rootPath);
    const auto previewDirectory = QDir(previewRoot).filePath(QStringLiteral("%1_%2")
        .arg(safeFileName(project.name),
             QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));

    ReportRenderEngine renderer;
    return renderer.renderPreviewImages(document, previewDirectory, pagePaths, errorMessage);
}

bool ReportExportService::buildDocument(const ReportExportRequest &request, ReportDocument *document, QString *errorMessage) const
{
    if (!document) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("报表文档为空。");
        }
        return false;
    }
    if (request.sourceRootId <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先在左侧选择素材目录。");
        }
        return false;
    }

    const auto project = m_projectService->currentProject();
    document->project.projectName = project.name;
    document->project.projectRoot = project.rootPath;
    document->project.projectCreatedAt = project.createdAt;
    document->project.shootTime = request.shootTime;
    document->project.location = request.location;
    document->project.director = request.director;
    document->project.cinematographer = request.cinematographer;
    document->project.ditName = request.ditName;
    document->project.exportTime = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    document->sections = request.sections;

    auto db = m_databaseManager->database();
    if (!db.isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目数据库未打开。");
        }
        return false;
    }

    if (!fetchSources(db, request.sourceRootId, document, errorMessage)) {
        return false;
    }
    if (!fetchAssets(db, request.sourceRootId, document, errorMessage)) {
        return false;
    }
    if (!fetchStreams(db, document, errorMessage)) {
        return false;
    }
    updateTotals(document);
    buildTree(document);
    return true;
}

bool ReportExportService::fetchSources(QSqlDatabase &db, qint64 sourceRootId, ReportDocument *document, QString *errorMessage) const
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, "
        "video_count, audio_count, image_count, other_count, warning_count "
        "FROM source_root WHERE id = ?"));
    query.addBindValue(sourceRootId);
    if (!execOrError(query, QStringLiteral("读取素材源"), errorMessage)) {
        return false;
    }

    if (!query.next()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("所选素材目录不存在或已被移除。");
        }
        return false;
    }

    ReportSourceSummary row;
    row.id = query.value(0).toLongLong();
    row.name = query.value(1).toString();
    row.path = query.value(2).toString();
    row.status = query.value(3).toString();
    row.totalFiles = query.value(4).toLongLong();
    row.totalFolders = query.value(5).toLongLong();
    row.totalSizeBytes = query.value(6).toLongLong();
    row.videoCount = query.value(7).toLongLong();
    row.audioCount = query.value(8).toLongLong();
    row.imageCount = query.value(9).toLongLong();
    row.otherCount = query.value(10).toLongLong();
    row.warningCount = query.value(11).toLongLong();
    document->sources.append(row);
    return true;
}

bool ReportExportService::fetchAssets(QSqlDatabase &db, qint64 sourceRootId, ReportDocument *document, QString *errorMessage) const
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT af.id, af.source_root_id, COALESCE(sr.name, ''), af.name, af.extension, af.absolute_path, "
        "af.relative_path, af.parent_path, af.asset_type, af.size_bytes, af.modified_at, "
        "COALESCE(th.image_path, ''), COALESCE(mm.container, ''), COALESCE(mm.duration_ms, 0), "
        "COALESCE(mm.bit_rate, 0), COALESCE(mm.probe_status, 0), COALESCE(mm.error_message, '') "
        "FROM asset_file af "
        "LEFT JOIN source_root sr ON sr.id = af.source_root_id "
        "LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        "LEFT JOIN thumbnail th ON th.asset_id = af.id AND th.status = 1 "
        "WHERE af.source_root_id = ? "
        "ORDER BY sr.name COLLATE NOCASE, af.relative_path COLLATE NOCASE, af.id"));
    query.addBindValue(sourceRootId);
    if (!execOrError(query, QStringLiteral("读取素材明细"), errorMessage)) {
        return false;
    }

    while (query.next()) {
        ReportAssetRow row;
        row.id = query.value(0).toLongLong();
        row.sourceRootId = query.value(1).toLongLong();
        row.sourceName = query.value(2).toString();
        row.name = query.value(3).toString();
        row.extension = query.value(4).toString();
        row.absolutePath = query.value(5).toString();
        row.relativePath = query.value(6).toString();
        row.parentPath = query.value(7).toString();
        row.assetType = static_cast<AssetType>(query.value(8).toInt());
        row.sizeBytes = query.value(9).toLongLong();
        row.modifiedAt = query.value(10).toString();
        row.thumbnailPath = query.value(11).toString();
        row.container = query.value(12).toString();
        row.durationMs = query.value(13).toLongLong();
        row.bitRate = query.value(14).toLongLong();
        row.probeStatus = static_cast<ProbeStatus>(query.value(15).toInt());
        row.metadataError = query.value(16).toString();
        document->assets.append(row);
    }
    return true;
}

bool ReportExportService::fetchStreams(QSqlDatabase &db, ReportDocument *document, QString *errorMessage) const
{
    QHash<qint64, int> assetIndex;
    for (int i = 0; i < document->assets.size(); ++i) {
        assetIndex.insert(document->assets.at(i).id, i);
    }
    if (assetIndex.isEmpty()) {
        return true;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT asset_id, stream_kind, codec, bit_rate, width, height, channels, sample_rate "
        "FROM media_stream ORDER BY asset_id, stream_index"));
    if (!execOrError(query, QStringLiteral("读取媒体流"), errorMessage)) {
        return false;
    }

    while (query.next()) {
        const auto assetId = query.value(0).toLongLong();
        if (!assetIndex.contains(assetId)) {
            continue;
        }
        ReportStreamSummary row;
        row.kind = query.value(1).toString();
        row.codec = query.value(2).toString();
        row.bitRate = query.value(3).toLongLong();
        row.width = query.value(4).toInt();
        row.height = query.value(5).toInt();
        row.channels = query.value(6).toInt();
        row.sampleRate = query.value(7).toInt();
        document->assets[assetIndex.value(assetId)].streams.append(row);
    }
    return true;
}

void ReportExportService::buildTree(ReportDocument *document) const
{
    document->treeLines.clear();

    QHash<qint64, QVector<ReportAssetRow>> assetsBySource;
    for (const auto &asset : document->assets) {
        assetsBySource[asset.sourceRootId].append(asset);
    }

    for (const auto &source : document->sources) {
        const auto title = source.name.isEmpty()
            ? source.path
            : QStringLiteral("%1  (%2)").arg(source.name, source.path);
        document->treeLines.append({title, 0, true});

        auto assets = assetsBySource.value(source.id);
        std::sort(assets.begin(), assets.end(), [](const ReportAssetRow &left, const ReportAssetRow &right) {
            return normalizedRelativePath(left.relativePath).compare(normalizedRelativePath(right.relativePath), Qt::CaseInsensitive) < 0;
        });

        QSet<QString> emittedFolders;
        for (const auto &asset : assets) {
            const auto relativePath = normalizedRelativePath(asset.relativePath);
            const auto parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                document->treeLines.append({asset.name, 1, false});
                continue;
            }

            QStringList folderPath;
            for (int i = 0; i < parts.size() - 1; ++i) {
                folderPath.append(parts.at(i));
                const auto key = folderPath.join(QLatin1Char('/'));
                if (emittedFolders.contains(key)) {
                    continue;
                }
                emittedFolders.insert(key);
                document->treeLines.append({parts.at(i), i + 1, true});
            }
            document->treeLines.append({parts.last(), static_cast<int>(parts.size()), false});
        }
    }

    if (document->sources.isEmpty()) {
        QSet<QString> emittedFolders;
        auto assets = document->assets;
        std::sort(assets.begin(), assets.end(), [](const ReportAssetRow &left, const ReportAssetRow &right) {
            return normalizedRelativePath(left.relativePath).compare(normalizedRelativePath(right.relativePath), Qt::CaseInsensitive) < 0;
        });
        for (const auto &asset : assets) {
            const auto parts = normalizedRelativePath(asset.relativePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
            QStringList folderPath;
            for (int i = 0; i < parts.size() - 1; ++i) {
                folderPath.append(parts.at(i));
                const auto key = folderPath.join(QLatin1Char('/'));
                if (!emittedFolders.contains(key)) {
                    emittedFolders.insert(key);
                    document->treeLines.append({parts.at(i), i, true});
                }
            }
            document->treeLines.append({parts.isEmpty() ? asset.name : parts.last(), static_cast<int>(parts.size()), false});
        }
    }
}

void ReportExportService::updateTotals(ReportDocument *document) const
{
    document->totalFiles = 0;
    document->totalFolders = 0;
    document->totalSizeBytes = 0;
    document->videoCount = 0;
    document->audioCount = 0;
    document->imageCount = 0;
    document->otherCount = 0;
    document->warningCount = 0;
    document->metadataFailedCount = 0;
    document->thumbnailMissingCount = 0;

    for (const auto &source : document->sources) {
        document->totalFiles += source.totalFiles;
        document->totalFolders += source.totalFolders;
        document->totalSizeBytes += source.totalSizeBytes;
        document->videoCount += source.videoCount;
        document->audioCount += source.audioCount;
        document->imageCount += source.imageCount;
        document->otherCount += source.otherCount;
        document->warningCount += source.warningCount;
    }

    if (document->sources.isEmpty()) {
        for (const auto &asset : document->assets) {
            ++document->totalFiles;
            document->totalSizeBytes += asset.sizeBytes;
            switch (asset.assetType) {
            case AssetType::Video: ++document->videoCount; break;
            case AssetType::Audio: ++document->audioCount; break;
            case AssetType::Image: ++document->imageCount; break;
            default: ++document->otherCount; break;
            }
        }
    }

    for (const auto &asset : document->assets) {
        if (asset.probeStatus == ProbeStatus::Failed || asset.probeStatus == ProbeStatus::Unavailable) {
            ++document->metadataFailedCount;
        }
        if (asset.assetType == AssetType::Video && asset.thumbnailPath.trimmed().isEmpty()) {
            ++document->thumbnailMissingCount;
        }
    }
}
