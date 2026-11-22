#pragma once

#include "domain/Entities.h"

#include <QAbstractListModel>
#include <QVector>

class AssetListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SourceRootIdRole,
        NameRole,
        RelativePathRole,
        ParentPathRole,
        TypeRole,
        TypeLabelRole,
        SizeBytesRole,
        SizeLabelRole,
        ModifiedAtRole,
        ReadableRole,
        ThumbnailPathRole,
        ThumbnailStatusRole,
        ThumbnailLoadingRole,
        DurationLabelRole,
        BitRateLabelRole,
        TechnicalSummaryRole,
        ProbeStatusLabelRole,
        FavoriteRole
    };

    explicit AssetListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void setItems(const QVector<AssetFile> &items);
    void appendItems(const QVector<AssetFile> &items);
    void updateItemsById(const QVector<AssetFile> &items);

private:
    QVector<AssetFile> m_items;
};
