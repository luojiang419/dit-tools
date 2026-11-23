#include "ui/models/SourceRootListModel.h"

#include "shared/Formatters.h"

SourceRootListModel::SourceRootListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SourceRootListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant SourceRootListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case NameRole: return item.name;
    case PathRole: return item.path;
    case StatusRole: return item.status;
    case StatusLabelRole: return Formatters::statusLabel(item.status);
    case StatusColorRole: return Formatters::statusColor(item.status);
    case TotalFilesRole: return item.totalFiles;
    case TotalFoldersRole: return item.totalFolders;
    case TotalSizeRole: return Formatters::formatBytes(item.totalSizeBytes);
    case VideoCountRole: return item.videoCount;
    case WarningCountRole: return item.warningCount;
    default: return {};
    }
}

QHash<int, QByteArray> SourceRootListModel::roleNames() const
{
    return {
        {IdRole, "sourceId"},
        {NameRole, "name"},
        {PathRole, "path"},
        {StatusRole, "status"},
        {StatusLabelRole, "statusLabel"},
        {StatusColorRole, "statusColor"},
        {TotalFilesRole, "totalFiles"},
        {TotalFoldersRole, "totalFolders"},
        {TotalSizeRole, "totalSize"},
        {VideoCountRole, "videoCount"},
        {WarningCountRole, "warningCount"}
    };
}

void SourceRootListModel::setItems(const QVector<SourceRoot> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

SourceRoot SourceRootListModel::itemAt(int row) const
{
    return (row >= 0 && row < m_items.size()) ? m_items.at(row) : SourceRoot{};
}
