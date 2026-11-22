#pragma once

#include <QObject>

class LibraryQueryService;
class SourceRootListModel;

class SourceRailViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(SourceRootListModel* model READ model CONSTANT)
    Q_PROPERTY(qint64 selectedSourceId READ selectedSourceId NOTIFY selectionChanged)

public:
    explicit SourceRailViewModel(LibraryQueryService *libraryQueryService, QObject *parent = nullptr);

    SourceRootListModel *model() const;
    qint64 selectedSourceId() const;

    void resetForProject();

    Q_INVOKABLE void reload();
    Q_INVOKABLE void selectSource(qint64 sourceRootId);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool removeSource(qint64 sourceRootId);

signals:
    void selectionChanged();
    void sourceSelected(qint64 sourceRootId);

private:
    LibraryQueryService *m_libraryQueryService = nullptr;
    SourceRootListModel *m_model = nullptr;
    qint64 m_selectedSourceId = 0;
};
