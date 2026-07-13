#pragma once

#include "domain/Entities.h"

#include <QAbstractListModel>
#include <QVector>

class JobListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        DetailRole,
        ProgressRole,
        StateRole,
        StateLabelRole,
        SourceRootIdRole,
        ErrorRole,
        JobTypeLabelRole,
        SubjectKindRole,
        SubjectKeyRole,
        SubjectNameRole,
        SubjectPathRole,
        SubjectThumbnailPathRole,
        SubjectTypeLabelRole,
        ProgressLabelRole,
        ProgressShortLabelRole,
        StepLabelRole
    };

    explicit JobListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<Job> &items);
    static void sortForDisplay(QVector<Job> &items);

private:
    QVector<Job> m_items;
};
