#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QVector>

class SourceRootListModel;

class MinimalSourceRailViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(SourceRootListModel* model READ model CONSTANT)
    Q_PROPERTY(qint64 selectedSourceId READ selectedSourceId NOTIFY selectionChanged)

public:
    explicit MinimalSourceRailViewModel(QObject *parent = nullptr);

    SourceRootListModel *model() const;
    qint64 selectedSourceId() const;
    SourceRoot sourceById(qint64 sourceRootId) const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void selectSource(qint64 sourceRootId);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool removeSource(qint64 sourceRootId);

signals:
    void selectionChanged();
    void sourceSelected(qint64 sourceRootId);

private:
    void seedSources();

    SourceRootListModel *m_model = nullptr;
    QVector<SourceRoot> m_sources;
    qint64 m_selectedSourceId = 0;
};
