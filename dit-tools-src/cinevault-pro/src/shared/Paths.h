#pragma once

#include <QtGlobal>
#include <QString>

class Paths {
public:
    static QString appDataRoot();
    static QString installRoot();
    static QString installDataRoot();
    static QString fallbackDataRoot();
    static QString resolvedDataRoot();
    static QString frameCacheRoot();
    static QString semanticSearchRoot();
    static QString semanticSearchIndexPath();
    static QString frameCacheDirectory(const QString &videoKey);
    static bool clearFrameCache(QString *errorMessage);
    static QString projectRootFromDatabasePath(const QString &databasePath);
    static QString projectThumbnailCacheRoot(const QString &databasePath);
    static QString projectThumbnailCachePath(const QString &databasePath, qint64 sourceRootId, qint64 assetId);
    static QString projectReportPreviewRoot(const QString &projectRoot);
    static QString projectFrameCacheDirectory(const QString &databasePath, const QString &videoKey);
    static QString projectContactSheetPath(const QString &databasePath, const QString &videoKey, int frameCount);
    static QString projectsRoot();
    static QString cacheRoot();
    static QString updatesRoot();
    static QString configRoot();
    static QString logsRoot();
    static bool ensureBaseDirectories(QString *errorMessage);
};
