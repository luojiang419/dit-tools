#include "ui/models/MaterialCenterListModel.h"

#include "shared/Formatters.h"

#include <QSet>

namespace {
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

bool canAnalyzeAsset(const GlobalVideoAsset &asset)
{
    return asset.assetType == AssetType::Video
        || asset.assetType == AssetType::Image
        || isSupportedTextAsset(asset.assetType, asset.extension);
}

QString captureTimeSourceLabel(const QString &source)
{
    if (source == QStringLiteral("quicktime_creation_date")) return QStringLiteral("QuickTime 拍摄时间");
    if (source == QStringLiteral("exif_datetime_original")) return QStringLiteral("EXIF 原始拍摄时间");
    if (source == QStringLiteral("media_creation_time")) return QStringLiteral("媒体创建时间");
    if (source == QStringLiteral("folder_date")) return QStringLiteral("目录日期推断");
    if (source == QStringLiteral("file_modified_time")) return QStringLiteral("文件修改时间兜底");
    return {};
}

QString timestampLabel(qint64 timestampMs)
{
    if (timestampMs < 0) return {};
    const auto seconds = timestampMs / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString quickPreviewPath(const GlobalVideoAsset &asset)
{
    if (!asset.thumbnailPath.trimmed().isEmpty()) {
        return asset.thumbnailPath;
    }
    return asset.assetType == AssetType::Image ? asset.absolutePath : QString();
}

QString quickDetail(const GlobalVideoAsset &asset)
{
    if (!asset.matchedFrameCaption.trimmed().isEmpty()) {
        const auto time = timestampLabel(asset.matchedTimestampMs);
        return time.isEmpty()
            ? asset.matchedFrameCaption
            : QStringLiteral("%1 · %2").arg(time, asset.matchedFrameCaption);
    }
    if (!asset.summary.trimmed().isEmpty()) {
        return asset.summary;
    }
    if (!asset.technicalSummary.trimmed().isEmpty()) {
        return asset.technicalSummary;
    }
    return asset.relativePath;
}

QString quickMeta(const GlobalVideoAsset &asset)
{
    QStringList values{
        Formatters::assetTypeLabel(asset.assetType),
        Formatters::videoAnalysisStatusLabel(asset.analysisStatus, asset.confirmationStatus)
    };
    if (!asset.captureDate.trimmed().isEmpty()) {
        values.append(asset.captureDate);
    }
    values.removeAll(QString());
    return values.join(QStringLiteral(" · "));
}
}

MaterialCenterListModel::MaterialCenterListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MaterialCenterListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MaterialCenterListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case VideoKeyRole: return item.videoKey;
    case AssetKeyRole: return item.assetKey.trimmed().isEmpty() ? item.videoKey : item.assetKey;
    case FileNameRole: return item.fileName;
    case AssetTypeRole: return static_cast<int>(item.assetType);
    case AssetTypeLabelRole: return Formatters::assetTypeLabel(item.assetType);
    case ExtensionRole: return item.extension;
    case ProjectNameRole: return item.projectName;
    case SourceNameRole: return item.sourceRootName;
    case RelativePathRole: return item.relativePath;
    case SummaryRole: return item.summary;
    case KeywordsRole: return item.keywords.join(QStringLiteral("、"));
    case AnalysisStatusLabelRole: return Formatters::videoAnalysisStatusLabel(item.analysisStatus, item.confirmationStatus);
    case ThumbnailPathRole: return item.thumbnailPath;
    case ThumbnailStatusRole: return static_cast<int>(item.thumbnailStatus);
    case ThumbnailLoadingRole: return item.thumbnailStatus == ThumbnailStatus::Running && item.thumbnailPath.isEmpty();
    case UpdatedAtRole: return item.updatedAt;
    case DurationMsRole: return item.durationMs;
    case CaptureDateRole: return item.captureDate;
    case CaptureTimeSourceLabelRole: return captureTimeSourceLabel(item.captureTimeSource);
    case SearchScoreRole: return item.searchScore;
    case SearchConfidenceRole: return item.searchConfidence;
    case SearchReasonsRole: return item.searchReasons.join(QStringLiteral(" · "));
    case MatchedTimestampMsRole: return item.matchedTimestampMs;
    case MatchedTimestampLabelRole: return timestampLabel(item.matchedTimestampMs);
    case MatchedFrameCaptionRole: return item.matchedFrameCaption;
    case ErrorMessageRole: return item.errorMessage;
    case CanAnalyzeRole: return canAnalyzeAsset(item);
    case ResultRankRole: return index.row() + 1;
    case QuickPreviewPathRole: return quickPreviewPath(item);
    case QuickDetailRole: return quickDetail(item);
    case QuickMetaRole: return quickMeta(item);
    case QuickReasonsRole: return item.searchReasons.join(QStringLiteral(" · "));
    default: return {};
    }
}

QHash<int, QByteArray> MaterialCenterListModel::roleNames() const
{
    return {
        {VideoKeyRole, "videoKey"},
        {AssetKeyRole, "assetKey"},
        {FileNameRole, "fileName"},
        {AssetTypeRole, "assetType"},
        {AssetTypeLabelRole, "assetTypeLabel"},
        {ExtensionRole, "extension"},
        {ProjectNameRole, "projectName"},
        {SourceNameRole, "sourceName"},
        {RelativePathRole, "relativePath"},
        {SummaryRole, "summary"},
        {KeywordsRole, "keywords"},
        {AnalysisStatusLabelRole, "analysisStatusLabel"},
        {ThumbnailPathRole, "thumbnailPath"},
        {ThumbnailStatusRole, "thumbnailStatus"},
        {ThumbnailLoadingRole, "thumbnailLoading"},
        {UpdatedAtRole, "updatedAt"},
        {DurationMsRole, "durationMs"},
        {CaptureDateRole, "captureDate"},
        {CaptureTimeSourceLabelRole, "captureTimeSourceLabel"},
        {SearchScoreRole, "searchScore"},
        {SearchConfidenceRole, "searchConfidence"},
        {SearchReasonsRole, "searchReasons"},
        {MatchedTimestampMsRole, "matchedTimestampMs"},
        {MatchedTimestampLabelRole, "matchedTimestampLabel"},
        {MatchedFrameCaptionRole, "matchedFrameCaption"},
        {ErrorMessageRole, "errorMessage"},
        {CanAnalyzeRole, "canAnalyze"},
        {ResultRankRole, "resultRank"},
        {QuickPreviewPathRole, "quickPreviewPath"},
        {QuickDetailRole, "quickDetail"},
        {QuickMetaRole, "quickMeta"},
        {QuickReasonsRole, "quickReasons"}
    };
}

void MaterialCenterListModel::setItems(const QVector<GlobalVideoAsset> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}
