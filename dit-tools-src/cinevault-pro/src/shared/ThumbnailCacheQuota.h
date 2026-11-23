#pragma once

#include <QString>
#include <QStringList>

struct ThumbnailCacheQuotaLimits {
    qint64 softLimitBytes = 0;
    qint64 hardLimitBytes = 0;
};

struct ThumbnailCacheQuotaResult {
    qint64 bytesBefore = 0;
    qint64 bytesAfter = 0;
    qint64 removedBytes = 0;
    qint64 removedFiles = 0;
    qint64 softLimitBytes = 0;
    qint64 hardLimitBytes = 0;
    bool withinHardLimit = true;
    bool requiresReferenceAudit = false;
    QStringList removedPaths;
    QStringList errors;
};

class ThumbnailCacheQuota final {
public:
    static constexpr qint64 SoftLimitMaximumBytes = 10LL * 1024LL * 1024LL * 1024LL;
    static constexpr qint64 HardLimitMaximumBytes = 20LL * 1024LL * 1024LL * 1024LL;

    static ThumbnailCacheQuotaLimits limitsForProject(const QString &projectDatabasePath);
    static ThumbnailCacheQuotaResult enforceForProject(
        const QString &projectDatabasePath,
        qint64 incomingBytes = 0,
        const QStringList &protectedPaths = {});
    static ThumbnailCacheQuotaResult enforceDirectory(
        const QString &cacheRoot,
        qint64 softLimitBytes,
        qint64 hardLimitBytes,
        qint64 incomingBytes = 0,
        const QStringList &protectedPaths = {});
    static void recordAccess(const QString &path);
    static bool isManagedThumbnailPath(const QString &path);
    static bool referenceAuditRequiredForProject(const QString &projectDatabasePath);
    static void completeReferenceAuditForProject(const QString &projectDatabasePath);
};
