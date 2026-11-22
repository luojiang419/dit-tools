#include "ui/viewmodels/MinimalSourceRailViewModel.h"

#include "ui/models/SourceRootListModel.h"

MinimalSourceRailViewModel::MinimalSourceRailViewModel(QObject *parent)
    : QObject(parent)
    , m_model(new SourceRootListModel(this))
{
    seedSources();
    reload();
}

SourceRootListModel *MinimalSourceRailViewModel::model() const
{
    return m_model;
}

qint64 MinimalSourceRailViewModel::selectedSourceId() const
{
    return m_selectedSourceId;
}

SourceRoot MinimalSourceRailViewModel::sourceById(qint64 sourceRootId) const
{
    for (const auto &source : m_sources) {
        if (source.id == sourceRootId) {
            return source;
        }
    }
    return {};
}

void MinimalSourceRailViewModel::reload()
{
    m_model->setItems(m_sources);
}

void MinimalSourceRailViewModel::selectSource(qint64 sourceRootId)
{
    if (m_selectedSourceId == sourceRootId) {
        return;
    }
    m_selectedSourceId = sourceRootId;
    emit selectionChanged();
    emit sourceSelected(sourceRootId);
}

void MinimalSourceRailViewModel::clearSelection()
{
    selectSource(0);
}

bool MinimalSourceRailViewModel::removeSource(qint64 sourceRootId)
{
    if (sourceRootId <= 0) {
        return false;
    }

    bool removed = false;
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources.at(i).id == sourceRootId) {
            m_sources.removeAt(i);
            removed = true;
            break;
        }
    }
    if (!removed) {
        return false;
    }

    reload();
    if (m_selectedSourceId == sourceRootId) {
        m_selectedSourceId = 0;
        emit selectionChanged();
    }
    emit sourceSelected(m_selectedSourceId);
    return true;
}

void MinimalSourceRailViewModel::seedSources()
{
    m_sources = {
        SourceRoot{1, QStringLiteral("A001_CARD"), QStringLiteral("G:/demo/A001_CARD"), QStringLiteral("ok"), 182, 14, 860LL * 1024 * 1024 * 1024, 180, 0, 2, 0, 0},
        SourceRoot{2, QStringLiteral("B_CAM_REEL"), QStringLiteral("G:/demo/B_CAM_REEL"), QStringLiteral("warning"), 96, 8, 412LL * 1024 * 1024 * 1024, 92, 0, 4, 0, 2},
        SourceRoot{3, QStringLiteral("SOUND_DAY01"), QStringLiteral("G:/demo/SOUND_DAY01"), QStringLiteral("scanning"), 64, 6, 88LL * 1024 * 1024 * 1024, 0, 64, 0, 0, 0}
    };
}
