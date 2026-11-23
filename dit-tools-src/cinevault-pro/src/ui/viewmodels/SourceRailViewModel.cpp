#include "ui/viewmodels/SourceRailViewModel.h"

#include "application/LibraryQueryService.h"
#include "ui/models/SourceRootListModel.h"

#include <QMetaObject>

SourceRailViewModel::SourceRailViewModel(LibraryQueryService *libraryQueryService, QObject *parent)
    : QObject(parent)
    , m_libraryQueryService(libraryQueryService)
    , m_model(new SourceRootListModel(this))
{
}

SourceRootListModel *SourceRailViewModel::model() const
{
    return m_model;
}

qint64 SourceRailViewModel::selectedSourceId() const
{
    return m_selectedSourceId;
}

void SourceRailViewModel::resetForProject()
{
    if (m_selectedSourceId != 0) {
        m_selectedSourceId = 0;
        emit selectionChanged();
        emit sourceSelected(0);
    }
    reload();
}

void SourceRailViewModel::reload()
{
    const auto items = m_libraryQueryService->fetchSourceRoots();
    m_model->setItems(items);
    bool foundSelection = false;
    for (const auto &item : items) {
        if (item.id == m_selectedSourceId) {
            foundSelection = true;
            break;
        }
    }
    if (!foundSelection && m_selectedSourceId != 0) {
        m_selectedSourceId = 0;
        emit selectionChanged();
        emit sourceSelected(0);
    }
}

void SourceRailViewModel::selectSource(qint64 sourceRootId)
{
    if (m_selectedSourceId == sourceRootId) {
        return;
    }
    m_selectedSourceId = sourceRootId;
    emit selectionChanged();
    emit sourceSelected(sourceRootId);
}

void SourceRailViewModel::clearSelection()
{
    selectSource(0);
}

bool SourceRailViewModel::removeSource(qint64 sourceRootId)
{
    if (!m_libraryQueryService || sourceRootId <= 0) {
        return false;
    }

    const bool removedSelection = m_selectedSourceId == sourceRootId;
    if (!m_libraryQueryService->removeSourceRoot(sourceRootId)) {
        return false;
    }

    if (removedSelection) {
        m_selectedSourceId = 0;
        emit selectionChanged();
    }
    QMetaObject::invokeMethod(this, [this, removedSelection]() {
        if (removedSelection) {
            emit sourceSelected(0);
        }
        reload();
    }, Qt::QueuedConnection);
    return true;
}
