#pragma once

#include <QObject>
#include <QVariantList>

class LibraryQueryService;

class InspectorViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY stateChanged)
    Q_PROPERTY(QVariantList details READ details NOTIFY stateChanged)

public:
    explicit InspectorViewModel(LibraryQueryService *libraryQueryService, QObject *parent = nullptr);

    QString title() const;
    QString subtitle() const;
    QVariantList details() const;

public slots:
    void showSource(qint64 sourceRootId);
    void showAsset(qint64 assetId);
    void reload();
    void clear();

signals:
    void stateChanged();

private:
    LibraryQueryService *m_libraryQueryService = nullptr;
    QString m_title = QStringLiteral("检查器");
    QString m_subtitle = QStringLiteral("选择左侧素材源或中间素材查看详情");
    QVariantList m_details;
    qint64 m_currentSourceRootId = 0;
    qint64 m_currentAssetId = 0;
};
