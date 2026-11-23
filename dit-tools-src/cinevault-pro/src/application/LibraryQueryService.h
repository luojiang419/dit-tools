#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <optional>

class DatabaseManager;
class SearchEngine;

struct LibraryAssetPageRequest {
    QString keyword;
    std::optional<qint64> sourceRootId;
    std::optional<AssetType> assetType;
    bool favoritesOnly = false;
    bool modifiedTimeAscending = false;
    bool hasCursor = false;
    QString cursorModifiedAt;
    qint64 cursorAssetId = 0;
    qsizetype limit = 200;
};

struct LibraryAssetPageResult {
    QVector<AssetFile> items;
    bool hasMore = false;
    QString errorMessage;
    qint64 elapsedMs = 0;
};

struct LibraryAssetCountResult {
    qint64 count = 0;
    QString errorMessage;
    qint64 elapsedMs = 0;
};

class LibraryQueryService : public QObject {
    Q_OBJECT

public:
    explicit LibraryQueryService(DatabaseManager *databaseManager, SearchEngine *searchEngine, QObject *parent = nullptr);

    QVector<SourceRoot> fetchSourceRoots() const;
    QString projectDatabasePath() const;
    LibraryAssetPageResult fetchAssetPageForPath(
        const QString &projectDatabasePath,
        const LibraryAssetPageRequest &request) const;
    LibraryAssetCountResult assetCountForPath(
        const QString &projectDatabasePath,
        const LibraryAssetPageRequest &request) const;
    InspectorState buildSourceInspector(qint64 sourceRootId) const;
    InspectorState buildAssetInspector(qint64 assetId) const;
    bool setAssetFavorite(qint64 assetId, bool favorite);
    bool removeAsset(qint64 assetId);
    bool removeSourceRoot(qint64 sourceRootId);

signals:
    void dataChanged();
    void sourceRootsChanged();

private:
    DatabaseManager *m_databaseManager = nullptr;
    SearchEngine *m_searchEngine = nullptr;
};
