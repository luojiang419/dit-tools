#pragma once

#include "domain/Entities.h"

#include <QAbstractListModel>
#include <QVector>

class SourceRootListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        StatusRole,
        StatusLabelRole,
        StatusColorRole,
        TotalFilesRole,
        TotalFoldersRole,
        TotalSizeRole,
        VideoCountRole,
        WarningCountRole
    };

    explicit SourceRootListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<SourceRoot> &items);
    SourceRoot itemAt(int row) const;

private:
    QVector<SourceRoot> m_items;
};
