#include "ui/models/MaterialCenterFrameListModel.h"

#include "shared/Formatters.h"

namespace {
QString timestampLabel(qint64 timestampMs)
{
    if (timestampMs < 0) {
        return {};
    }
    const auto seconds = timestampMs / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString entitySummary(const QVector<VisionEntityFact> &entities)
{
    QStringList values;
    for (const auto &entity : entities) {
        QStringList attributes;
        attributes.append(entity.colors);
        attributes.append(entity.materials);
        attributes.append(entity.attributes);
        const auto value = attributes.isEmpty()
            ? entity.label
            : QStringLiteral("%1（%2）")
                  .arg(entity.label, attributes.join(QStringLiteral("、")));
        if (!value.trimmed().isEmpty()) {
            values.append(value);
        }
    }
    return values.join(QStringLiteral("；"));
}

QString quickDetail(const FrameSearchHit &frame)
{
    if (!frame.caption.trimmed().isEmpty()) {
        return frame.caption;
    }
    const auto entities = entitySummary(frame.entities);
    if (!entities.isEmpty()) {
        return entities;
    }
    if (!frame.ocrText.trimmed().isEmpty()) {
        return QStringLiteral("画面文字：%1").arg(frame.ocrText);
    }
    return frame.relativePath;
}

QString quickMeta(const FrameSearchHit &frame)
{
    QStringList values{
        timestampLabel(frame.timestampMs),
        Formatters::assetTypeLabel(frame.assetType)
    };
    values.removeAll(QString());
    return values.join(QStringLiteral(" · "));
}
}

MaterialCenterFrameListModel::MaterialCenterFrameListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MaterialCenterFrameListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MaterialCenterFrameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const auto &item = m_items.at(index.row());
    switch (role) {
    case FrameKeyRole: return item.frameKey;
    case VideoKeyRole: return item.videoKey;
    case AssetKeyRole: return item.assetKey;
    case FileNameRole: return item.fileName;
    case AssetTypeLabelRole: return Formatters::assetTypeLabel(item.assetType);
    case ProjectNameRole: return item.projectName;
    case SourceNameRole: return item.sourceRootName;
    case RelativePathRole: return item.relativePath;
    case FrameNumberRole: return item.frameNumber;
    case TimestampMsRole: return item.timestampMs;
    case TimestampLabelRole: return timestampLabel(item.timestampMs);
    case ImagePathRole: return item.imagePath;
    case CaptionRole: return item.caption;
    case TagsRole: return item.tags.join(QStringLiteral("、"));
    case ObjectsRole: return item.objects.join(QStringLiteral("、"));
    case ActionsRole: return item.actions;
    case SettingRole: return item.setting;
    case EntitySummaryRole: return entitySummary(item.entities);
    case OcrTextRole: return item.ocrText;
    case FactsCompleteRole: return item.factsComplete;
    case ScoreRole: return item.score;
    case ConfidenceRole: return item.confidence;
    case ReasonsRole: return item.reasons.join(QStringLiteral(" · "));
    case ResultRankRole: return index.row() + 1;
    case QuickPreviewPathRole: return item.imagePath;
    case QuickDetailRole: return quickDetail(item);
    case QuickMetaRole: return quickMeta(item);
    case QuickReasonsRole: return item.reasons.join(QStringLiteral(" · "));
    default: return {};
    }
}

QHash<int, QByteArray> MaterialCenterFrameListModel::roleNames() const
{
    return {
        {FrameKeyRole, "frameKey"},
        {VideoKeyRole, "videoKey"},
        {AssetKeyRole, "assetKey"},
        {FileNameRole, "fileName"},
        {AssetTypeLabelRole, "assetTypeLabel"},
        {ProjectNameRole, "projectName"},
        {SourceNameRole, "sourceName"},
        {RelativePathRole, "relativePath"},
        {FrameNumberRole, "frameNumber"},
        {TimestampMsRole, "timestampMs"},
        {TimestampLabelRole, "timestampLabel"},
        {ImagePathRole, "imagePath"},
        {CaptionRole, "caption"},
        {TagsRole, "tags"},
        {ObjectsRole, "objects"},
        {ActionsRole, "actions"},
        {SettingRole, "setting"},
        {EntitySummaryRole, "entitySummary"},
        {OcrTextRole, "ocrText"},
        {FactsCompleteRole, "factsComplete"},
        {ScoreRole, "score"},
        {ConfidenceRole, "confidence"},
        {ReasonsRole, "reasons"},
        {ResultRankRole, "resultRank"},
        {QuickPreviewPathRole, "quickPreviewPath"},
        {QuickDetailRole, "quickDetail"},
        {QuickMetaRole, "quickMeta"},
        {QuickReasonsRole, "quickReasons"}
    };
}

void MaterialCenterFrameListModel::setItems(const QVector<FrameSearchHit> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}
