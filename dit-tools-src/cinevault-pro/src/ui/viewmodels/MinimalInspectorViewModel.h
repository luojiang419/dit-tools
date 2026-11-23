#pragma once

#include <QObject>
#include <QVariantList>

class MinimalLibraryWorkspaceViewModel;
class MinimalSourceRailViewModel;

class MinimalInspectorViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY stateChanged)
    Q_PROPERTY(QVariantList details READ details NOTIFY stateChanged)

public:
    explicit MinimalInspectorViewModel(MinimalSourceRailViewModel *sourceRailViewModel,
                                       MinimalLibraryWorkspaceViewModel *libraryWorkspaceViewModel,
                                       QObject *parent = nullptr);

    QString title() const;
    QString subtitle() const;
    QVariantList details() const;

public slots:
    void showSource(qint64 sourceRootId);
    void showAsset(qint64 assetId);
    void clear();

signals:
    void stateChanged();

private:
    MinimalSourceRailViewModel *m_sourceRailViewModel = nullptr;
    MinimalLibraryWorkspaceViewModel *m_libraryWorkspaceViewModel = nullptr;
    QString m_title;
    QString m_subtitle;
    QVariantList m_details;
};
