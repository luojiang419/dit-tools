#pragma once

#include "domain/SearchTypes.h"

#include <QAbstractListModel>
#include <QVector>

class MaterialCenterFolderListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        FolderKeyRole = Qt::UserRole + 1,
        ProjectUuidRole,
        ProjectNameRole,
        ProjectDatabasePathRole,
        SourceRootIdRole,
        SourceNameRole,
        NameRole,
        AbsolutePathRole,
        RelativePathRole,
        ParentRelativePathRole,
        DepthRole,
        DirectFileCountRole,
        RecursiveFileCountRole,
        NormalizedDateRole,
        AvailableRole,
        ScoreRole,
        ConfidenceRole,
        ReasonsRole,
        ResultRankRole
    };

    explicit MaterialCenterFolderListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<FolderSearchHit> &items);

private:
    QVector<FolderSearchHit> m_items;
};
