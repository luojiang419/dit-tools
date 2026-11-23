#pragma once

#include "domain/SearchTypes.h"

#include <QAbstractListModel>
#include <QVector>

class MaterialCenterFrameListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        FrameKeyRole = Qt::UserRole + 1,
        VideoKeyRole,
        AssetKeyRole,
        FileNameRole,
        AssetTypeLabelRole,
        ProjectNameRole,
        SourceNameRole,
        RelativePathRole,
        FrameNumberRole,
        TimestampMsRole,
        TimestampLabelRole,
        ImagePathRole,
        CaptionRole,
        TagsRole,
        ObjectsRole,
        ActionsRole,
        SettingRole,
        EntitySummaryRole,
        OcrTextRole,
        FactsCompleteRole,
        ScoreRole,
        ConfidenceRole,
        ReasonsRole,
        ResultRankRole,
        QuickPreviewPathRole,
        QuickDetailRole,
        QuickMetaRole,
        QuickReasonsRole
    };

    explicit MaterialCenterFrameListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<FrameSearchHit> &items);

private:
    QVector<FrameSearchHit> m_items;
};
