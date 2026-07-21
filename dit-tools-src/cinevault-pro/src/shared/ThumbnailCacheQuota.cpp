#include "shared/ThumbnailCacheQuota.h"

#include "shared/FolderPathMetadata.h"
#include "shared/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>
#include <queue>
#include <utility>
#include <vector>

namespace {
constexpr qsizetype CandidateBatchSize = 4096;
constexpr auto ReferenceAuditMarkerName = ".reference-audit-required";

struct CacheCandidate {
    QString path;
    qint64 sizeBytes = 0;
    qint64 accessedAtMs = 0;
};

struct NewestCandidateFirst {
    bool operator()(const CacheCandidate &left, const CacheCandidate &right) const
    {
        if (left.accessedAtMs != right.accessedAtMs) {
            return left.accessedAtMs < right.accessedAtMs;
        }
        return left.path < right.path;
    }
};

qint64 accessTimeMs(const QFileInfo &info)
{
    const auto accessed = info.lastRead();
    if (accessed.isValid()) {
        return accessed.toMSecsSinceEpoch();
    }
    const auto modified = info.lastModified();
    return modified.isValid() ? modified.toMSecsSinceEpoch() : qint64{0};
}

QString pathKey(const QString &path)
{
    return FolderPathMetadata::normalizedPathKey(QFileInfo(path).absoluteFilePath());
}
}

ThumbnailCacheQuotaLimits ThumbnailCacheQuota::limitsForProject(
    const QString &projectDatabasePath)
{
    const auto projectRoot = Paths::projectRootFromDatabasePath(projectDatabasePath);
    QStorageInfo storage(projectRoot);
    storage.refresh();
    qint64 softLimit = SoftLimitMaximumBytes;
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() >= 0) {
        softLimit = qMin(SoftLimitMaximumBytes, storage.bytesAvailable() / 20);
    }
    softLimit = qMax<qint64>(0, softLimit);
    const auto doubledSoftLimit = softLimit > HardLimitMaximumBytes / 2
        ? HardLimitMaximumBytes
        : softLimit * 2;
    const auto hardLimit = qMin(HardLimitMaximumBytes,
                                qMax(softLimit, doubledSoftLimit));
    return {softLimit, hardLimit};
}

ThumbnailCacheQuotaResult ThumbnailCacheQuota::enforceForProject(
    const QString &projectDatabasePath,
    qint64 incomingBytes,
    const QStringList &protectedPaths)
{
    const auto limits = limitsForProject(projectDatabasePath);
    return enforceDirectory(Paths::projectThumbnailCacheRoot(projectDatabasePath),
                            limits.softLimitBytes,
                            limits.hardLimitBytes,
                            incomingBytes,
                            protectedPaths);
}

ThumbnailCacheQuotaResult ThumbnailCacheQuota::enforceDirectory(
    const QString &cacheRoot,
    qint64 softLimitBytes,
    qint64 hardLimitBytes,
    qint64 incomingBytes,
    const QStringList &protectedPaths)
{
    ThumbnailCacheQuotaResult result;
    result.softLimitBytes = qMax<qint64>(0, softLimitBytes);
    result.hardLimitBytes = qBound(result.softLimitBytes,
                                   qMax<qint64>(0, hardLimitBytes),
                                   HardLimitMaximumBytes);
    incomingBytes = qMax<qint64>(0, incomingBytes);

    const QFileInfo rootInfo(cacheRoot);
    if (!rootInfo.exists()) {
        result.withinHardLimit = incomingBytes <= result.hardLimitBytes;
        return result;
    }
    if (!rootInfo.isDir() || rootInfo.isSymLink()) {
        result.errors.append(QStringLiteral("缩略图缓存根目录无效：%1").arg(cacheRoot));
        result.withinHardLimit = false;
        return result;
    }

    QSet<QString> protectedKeys;
    for (const auto &path : protectedPaths) {
        protectedKeys.insert(pathKey(path));
    }
    const auto referenceAuditMarker = QDir(cacheRoot).filePath(
        QString::fromLatin1(ReferenceAuditMarkerName));
    result.requiresReferenceAudit = QFileInfo::exists(referenceAuditMarker);
    protectedKeys.insert(pathKey(referenceAuditMarker));
    QDirIterator sizeIterator(cacheRoot,
                              QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
    while (sizeIterator.hasNext()) {
        const auto path = sizeIterator.next();
        if (pathKey(path) == pathKey(referenceAuditMarker)) {
            continue;
        }
        result.bytesBefore += qMax<qint64>(0, sizeIterator.fileInfo().size());
    }
    result.bytesAfter = result.bytesBefore;
    const auto targetBytes = qMax<qint64>(0, result.softLimitBytes - incomingBytes);

    while (result.bytesAfter > targetBytes) {
        std::priority_queue<CacheCandidate,
                            std::vector<CacheCandidate>,
                            NewestCandidateFirst> oldestCandidates;
        QDirIterator candidateIterator(cacheRoot,
                                       QDir::Files | QDir::Hidden | QDir::System
                                           | QDir::NoSymLinks,
                                       QDirIterator::Subdirectories);
        while (candidateIterator.hasNext()) {
            const auto path = candidateIterator.next();
            if (protectedKeys.contains(pathKey(path))) {
                continue;
            }
            const auto info = candidateIterator.fileInfo();
            CacheCandidate candidate{info.absoluteFilePath(),
                                     qMax<qint64>(0, info.size()),
                                     accessTimeMs(info)};
            if (oldestCandidates.size() < static_cast<size_t>(CandidateBatchSize)) {
                oldestCandidates.push(std::move(candidate));
                continue;
            }
            const auto &newestSelected = oldestCandidates.top();
            if (candidate.accessedAtMs < newestSelected.accessedAtMs
                || (candidate.accessedAtMs == newestSelected.accessedAtMs
                    && candidate.path < newestSelected.path)) {
                oldestCandidates.pop();
                oldestCandidates.push(std::move(candidate));
            }
        }

        QVector<CacheCandidate> deletionBatch;
        deletionBatch.reserve(static_cast<qsizetype>(oldestCandidates.size()));
        while (!oldestCandidates.empty()) {
            deletionBatch.append(std::move(oldestCandidates.top()));
            oldestCandidates.pop();
        }
        std::sort(deletionBatch.begin(), deletionBatch.end(),
                  [](const CacheCandidate &left, const CacheCandidate &right) {
            if (left.accessedAtMs != right.accessedAtMs) {
                return left.accessedAtMs < right.accessedAtMs;
            }
            return left.path < right.path;
        });
        if (deletionBatch.isEmpty()) {
            break;
        }

        bool removedAny = false;
        for (const auto &candidate : std::as_const(deletionBatch)) {
            if (result.bytesAfter <= targetBytes) {
                break;
            }
            if (!QFileInfo::exists(referenceAuditMarker)) {
                QFile marker(referenceAuditMarker);
                if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    marker.write(QDateTime::currentDateTimeUtc()
                                     .toString(Qt::ISODateWithMs)
                                     .toUtf8());
                }
            }
            if (!QFile::remove(candidate.path)) {
                result.errors.append(QStringLiteral("无法回收缩略图缓存：%1").arg(candidate.path));
                continue;
            }
            removedAny = true;
            ++result.removedFiles;
            result.removedBytes += candidate.sizeBytes;
            result.bytesAfter = qMax<qint64>(0, result.bytesAfter - candidate.sizeBytes);
            if (result.removedPaths.size() < CandidateBatchSize) {
                result.removedPaths.append(candidate.path);
            } else {
                result.requiresReferenceAudit = true;
            }
        }
        if (!removedAny) {
            break;
        }
    }

    result.withinHardLimit = result.bytesAfter <= result.hardLimitBytes - incomingBytes;
    return result;
}

void ThumbnailCacheQuota::recordAccess(const QString &path)
{
    if (!isManagedThumbnailPath(path)) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileAccessTime);
}

bool ThumbnailCacheQuota::isManagedThumbnailPath(const QString &path)
{
    const auto portablePath = QDir::fromNativeSeparators(QDir::cleanPath(path)).toCaseFolded();
    return portablePath.contains(QStringLiteral("/cache/thumbnails/"));
}

bool ThumbnailCacheQuota::referenceAuditRequiredForProject(
    const QString &projectDatabasePath)
{
    return QFileInfo::exists(
        QDir(Paths::projectThumbnailCacheRoot(projectDatabasePath))
            .filePath(QString::fromLatin1(ReferenceAuditMarkerName)));
}

void ThumbnailCacheQuota::completeReferenceAuditForProject(
    const QString &projectDatabasePath)
{
    QFile::remove(QDir(Paths::projectThumbnailCacheRoot(projectDatabasePath))
                      .filePath(QString::fromLatin1(ReferenceAuditMarkerName)));
}
