#pragma once

#include "domain/Entities.h"

#include <QAbstractListModel>
#include <QVector>

class FeedbackMessageListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SenderRole,
        SenderLabelRole,
        OutgoingRole,
        TextRole,
        CreatedAtRole,
        CreatedAtLabelRole,
        AttachmentsRole,
        HasTextRole,
        HasAttachmentsRole
    };

    explicit FeedbackMessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<FeedbackMessage> &items);

private:
    QVector<FeedbackMessage> m_items;
};
