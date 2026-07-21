#pragma once

#include "application/LibraryQueryService.h"
#include "domain/Entities.h"

#include <QFutureSynchronizer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <optional>

class AssetListModel;
class LibraryWorkspaceViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(AssetListModel* model READ model CONSTANT)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filtersChanged)
    Q_PROPERTY(bool modifiedTimeAscending READ modifiedTimeAscending NOTIFY filtersChanged)
    Q_PROPERTY(QString sortOrderText READ sortOrderText NOTIFY filtersChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(qint64 totalAssetCount READ totalAssetCount NOTIFY statusChanged)
    Q_PROPERTY(qint64 readyAssetCount READ readyAssetCount NOTIFY statusChanged)
    Q_PROPERTY(qint64 pendingAssetCount READ pendingAssetCount NOTIFY statusChanged)
    Q_PROPERTY(qint64 issueAssetCount READ issueAssetCount NOTIFY statusChanged)
    Q_PROPERTY(qint64 selectedAssetId READ selectedAssetId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedAssetIndex READ selectedAssetIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPreviewTitle READ selectedPreviewTitle NOTIFY selectionChanged)
    Q_PROPERTY(QUrl selectedPreviewUrl READ selectedPreviewUrl NOTIFY selectionChanged)
    Q_PROPERTY(QUrl selectedPreviewThumbnailUrl READ selectedPreviewThumbnailUrl NOTIFY selectionChanged)
    Q_PROPERTY(QUrl selectedAssetUrl READ selectedAssetUrl NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedPreviewIsVideo READ selectedPreviewIsVideo NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedPreviewIsImage READ selectedPreviewIsImage NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedPreviewIsDocument READ selectedPreviewIsDocument NOTIFY selectionChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY paginationChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY paginationChanged)
    Q_PROPERTY(int loadedAssetCount READ loadedAssetCount NOTIFY paginationChanged)

public:
    explicit LibraryWorkspaceViewModel(LibraryQueryService *libraryQueryService, QObject *parent = nullptr);
    ~LibraryWorkspaceViewModel() override;

    AssetListModel *model() const;
    int viewMode() const;
    bool favoritesOnly() const;
    bool modifiedTimeAscending() const;
    QString sortOrderText() const;
    QString statusText() const;
    qint64 totalAssetCount() const;
    qint64 readyAssetCount() const;
    qint64 pendingAssetCount() const;
    qint64 issueAssetCount() const;
    qint64 selectedAssetId() const;
    int selectedAssetIndex() const;
    QString selectedPreviewTitle() const;
    QUrl selectedPreviewUrl() const;
    QUrl selectedPreviewThumbnailUrl() const;
    QUrl selectedAssetUrl() const;
    bool selectedPreviewIsVideo() const;
    bool selectedPreviewIsImage() const;
    bool selectedPreviewIsDocument() const;
    bool loading() const;
    bool hasMore() const;
    int loadedAssetCount() const;
    void waitForIdle();

    void setViewMode(int viewMode);
    void setFavoritesOnly(bool favoritesOnly);
    void resetForProject();

    Q_INVOKABLE void reload();
    Q_INVOKABLE void scheduleReload();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void setSearchText(const QString &searchText);
    Q_INVOKABLE void setSourceFilter(qint64 sourceRootId);
    Q_INVOKABLE void setAssetTypeFilter(int assetType);
    Q_INVOKABLE void toggleModifiedTimeOrder();
    Q_INVOKABLE void selectAsset(qint64 assetId);
    Q_INVOKABLE void selectAssetAt(int index);
    Q_INVOKABLE void moveAssetSelection(int delta);
    Q_INVOKABLE bool toggleAssetFavorite(qint64 assetId);
    Q_INVOKABLE bool removeAsset(qint64 assetId);
    Q_INVOKABLE bool openAssetFolder(qint64 assetId);
    Q_INVOKABLE bool copyAssetPath(qint64 assetId);

signals:
    void viewModeChanged();
    void filtersChanged();
    void statusChanged();
    void selectionChanged();
    void paginationChanged();
    void assetSelected(qint64 assetId);

private:
    AssetFile assetById(qint64 assetId) const;
    AssetFile selectedAsset() const;
    LibraryAssetPageRequest queryRequest(bool includeCursor) const;
    void beginResetQuery();
    void startPageQuery(quint64 generation, bool firstPage);
    void startCountQuery(quint64 generation);
    void applyPageResult(quint64 generation,
                         bool firstPage,
                         const LibraryAssetPageResult &result);
    void applyCountResult(quint64 generation,
                          const LibraryAssetCountResult &result);
    void updateDerivedState();
    void setLoading(bool loading);
    void setHasMore(bool hasMore);

    LibraryQueryService *m_libraryQueryService = nullptr;
    AssetListModel *m_model = nullptr;
    QVector<AssetFile> m_assets;
    QString m_searchText;
    int m_viewMode = 0;
    bool m_favoritesOnly = false;
    bool m_modifiedTimeAscending = false;
    qint64 m_selectedAssetId = 0;
    std::optional<qint64> m_sourceFilter;
    std::optional<AssetType> m_assetTypeFilter;
    qint64 m_totalCount = 0;
    qint64 m_readyCount = 0;
    qint64 m_pendingCount = 0;
    qint64 m_issueCount = 0;
    QTimer m_catalogReloadTimer;
    QTimer m_searchReloadTimer;
    QTimer m_countQueryTimer;
    QFutureSynchronizer<void> m_futures;
    quint64 m_queryGeneration = 0;
    quint64 m_pendingCountGeneration = 0;
    bool m_loading = false;
    bool m_hasMore = false;
    bool m_countPending = false;
    QString m_queryError;
};
