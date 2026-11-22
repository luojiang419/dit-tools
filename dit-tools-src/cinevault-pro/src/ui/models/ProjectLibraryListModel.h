#pragma once

#include "domain/Entities.h"

#include <QAbstractListModel>
#include <QVector>

class ProjectLibraryListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        RootPathRole,
        DatabasePathRole,
        CreatedAtRole,
        AvailableRole,
        CurrentRole,
        StatusLabelRole,
        StatusColorRole
    };

    explicit ProjectLibraryListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<ProjectLibraryEntry> &items);

private:
    QVector<ProjectLibraryEntry> m_items;
};

