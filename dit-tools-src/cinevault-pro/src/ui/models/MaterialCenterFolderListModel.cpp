#include "ui/models/MaterialCenterFolderListModel.h"

MaterialCenterFolderListModel::MaterialCenterFolderListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MaterialCenterFolderListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MaterialCenterFolderListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case FolderKeyRole: return item.folderKey;
    case ProjectUuidRole: return item.projectUuid;
    case ProjectNameRole: return item.projectName;
    case ProjectDatabasePathRole: return item.projectDatabasePath;
    case SourceRootIdRole: return item.sourceRootId;
    case SourceNameRole: return item.sourceRootName;
    case NameRole: return item.name;
    case AbsolutePathRole: return item.absolutePath;
    case RelativePathRole: return item.relativePath;
    case ParentRelativePathRole: return item.parentRelativePath;
    case DepthRole: return item.depth;
    case DirectFileCountRole: return item.directFileCount;
    case RecursiveFileCountRole: return item.recursiveFileCount;
    case NormalizedDateRole: return item.normalizedDate;
    case AvailableRole: return item.available;
    case ScoreRole: return item.score;
    case ConfidenceRole: return item.confidence;
    case ReasonsRole: return item.reasons.join(QStringLiteral(" · "));
    case ResultRankRole: return index.row() + 1;
    default: return {};
    }
}

QHash<int, QByteArray> MaterialCenterFolderListModel::roleNames() const
{
    return {
        {FolderKeyRole, "folderKey"},
        {ProjectUuidRole, "projectUuid"},
        {ProjectNameRole, "projectName"},
        {ProjectDatabasePathRole, "projectDatabasePath"},
        {SourceRootIdRole, "sourceRootId"},
        {SourceNameRole, "sourceName"},
        {NameRole, "name"},
        {AbsolutePathRole, "absolutePath"},
        {RelativePathRole, "relativePath"},
        {ParentRelativePathRole, "parentRelativePath"},
        {DepthRole, "depth"},
        {DirectFileCountRole, "directFileCount"},
        {RecursiveFileCountRole, "recursiveFileCount"},
        {NormalizedDateRole, "normalizedDate"},
        {AvailableRole, "available"},
        {ScoreRole, "score"},
        {ConfidenceRole, "confidence"},
        {ReasonsRole, "reasons"},
        {ResultRankRole, "resultRank"}
    };
}

void MaterialCenterFolderListModel::setItems(const QVector<FolderSearchHit> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}
