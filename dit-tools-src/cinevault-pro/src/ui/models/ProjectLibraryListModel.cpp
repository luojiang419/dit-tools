#include "ui/models/ProjectLibraryListModel.h"

ProjectLibraryListModel::ProjectLibraryListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ProjectLibraryListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant ProjectLibraryListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto &item = m_items.at(index.row());
    switch (role) {
    case NameRole: return item.name;
    case RootPathRole: return item.rootPath;
    case DatabasePathRole: return item.databasePath;
    case CreatedAtRole: return item.createdAt;
    case AvailableRole: return item.available;
    case CurrentRole: return item.current;
    case StatusLabelRole:
        if (item.current) return QStringLiteral("当前项目");
        return item.available ? QStringLiteral("可打开") : QStringLiteral("缺失");
    case StatusColorRole:
        if (item.current) return QStringLiteral("#4F8CFF");
        return item.available ? QStringLiteral("#22C55E") : QStringLiteral("#EF4444");
    default: return {};
    }
}

QHash<int, QByteArray> ProjectLibraryListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {RootPathRole, "rootPath"},
        {DatabasePathRole, "databasePath"},
        {CreatedAtRole, "createdAt"},
        {AvailableRole, "available"},
        {CurrentRole, "current"},
        {StatusLabelRole, "statusLabel"},
        {StatusColorRole, "statusColor"}
    };
}

void ProjectLibraryListModel::setItems(const QVector<ProjectLibraryEntry> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

