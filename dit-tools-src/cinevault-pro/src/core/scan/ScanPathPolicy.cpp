#include "core/scan/ScanPathPolicy.h"

#include "shared/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QString normalizedAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString comparisonPath(const QString &path)
{
    auto normalized = QDir::fromNativeSeparators(normalizedAbsolutePath(path));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool isSamePath(const QString &firstPath, const QString &secondPath)
{
    return comparisonPath(firstPath) == comparisonPath(secondPath);
}

QStringList relativeComponents(const QString &rootPath, const QString &candidatePath)
{
    const auto relativePath = QDir(rootPath).relativeFilePath(candidatePath);
    if (relativePath == QStringLiteral(".")) {
        return {};
    }
    return QDir::fromNativeSeparators(relativePath)
        .split(QLatin1Char('/'), Qt::SkipEmptyParts);
}
}

bool ScanPathPolicy::isPathInside(const QString &candidatePath, const QString &rootPath)
{
    if (candidatePath.trimmed().isEmpty() || rootPath.trimmed().isEmpty()) {
        return false;
    }
    const auto candidate = comparisonPath(candidatePath);
    const auto root = comparisonPath(rootPath);
    const auto rootPrefix = root.endsWith(QLatin1Char('/'))
        ? root
        : root + QLatin1Char('/');
    return candidate == root || candidate.startsWith(rootPrefix);
}

bool ScanPathPolicy::pathsOverlap(const QString &firstPath, const QString &secondPath)
{
    return isPathInside(firstPath, secondPath) || isPathInside(secondPath, firstPath);
}

bool ScanPathPolicy::isLinkOrReparsePoint(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink()) {
        return true;
    }
#ifdef Q_OS_WIN
    auto nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    if (!nativePath.startsWith(QStringLiteral("\\\\?\\"))) {
        nativePath = nativePath.startsWith(QStringLiteral("\\\\"))
            ? QStringLiteral("\\\\?\\UNC\\") + nativePath.mid(2)
            : QStringLiteral("\\\\?\\") + nativePath;
    }
    const auto attributes = GetFileAttributesW(
        reinterpret_cast<const wchar_t *>(nativePath.utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

bool ScanPathPolicy::isExcludedPath(const QString &sourceRootPath,
                                    const QString &candidatePath,
                                    const QString &projectDatabasePath)
{
    if (!isPathInside(candidatePath, sourceRootPath)) {
        return true;
    }
    if (comparisonPath(sourceRootPath)
            .contains(QStringLiteral("harddiskvolumeshadowcopy"))) {
        return true;
    }

    QStorageInfo sourceStorage(sourceRootPath);
    sourceStorage.refresh();
    const auto wholeVolumeSource = sourceStorage.isValid()
        && isSamePath(sourceRootPath, sourceStorage.rootPath());

    static const QSet<QString> excludedDirectoryNames{
        QStringLiteral("$recycle.bin"),
        QStringLiteral("recycler"),
        QStringLiteral("system volume information"),
        QStringLiteral("$extend")
    };
    if (wholeVolumeSource) {
        const auto components = relativeComponents(sourceRootPath, candidatePath);
        for (const auto &component : components) {
            const auto folded = component.toCaseFolded();
            if (excludedDirectoryNames.contains(folded)
                || folded.contains(QStringLiteral("harddiskvolumeshadowcopy"))) {
                return true;
            }
        }

        const QStringList excludedRoots{
            QDir::tempPath(),
            QStandardPaths::writableLocation(QStandardPaths::TempLocation),
            Paths::cacheRoot(),
            Paths::resolvedDataRoot()
        };
        for (const auto &excludedRoot : excludedRoots) {
            if (!excludedRoot.trimmed().isEmpty() && isPathInside(candidatePath, excludedRoot)) {
                return true;
            }
        }
    }

    if (!projectDatabasePath.trimmed().isEmpty()) {
        const auto databasePath = normalizedAbsolutePath(projectDatabasePath);
        if (isSamePath(candidatePath, databasePath)
            || isSamePath(candidatePath, databasePath + QStringLiteral("-wal"))
            || isSamePath(candidatePath, databasePath + QStringLiteral("-shm"))) {
            return true;
        }
        const auto projectRoot = QFileInfo(databasePath).absolutePath();
        const QStringList projectGeneratedRoots{
            QDir(projectRoot).filePath(QStringLiteral("cache")),
            QDir(projectRoot).filePath(QStringLiteral("analysis"))
        };
        for (const auto &generatedRoot : projectGeneratedRoots) {
            if (isPathInside(candidatePath, generatedRoot)) {
                return true;
            }
        }
    }
    return false;
}

QStringList ScanPathPolicy::normalizeDirtyDirectories(const QString &sourceRootPath,
                                                      const QStringList &changedPaths,
                                                      const QString &projectDatabasePath)
{
    const auto rootPath = normalizedAbsolutePath(sourceRootPath);
    QSet<QString> candidates;
    for (const auto &changedPath : changedPaths) {
        auto candidate = normalizedAbsolutePath(changedPath);
        if (!isPathInside(candidate, rootPath)
            || isExcludedPath(rootPath, candidate, projectDatabasePath)) {
            continue;
        }
        const QFileInfo info(candidate);
        if (!info.exists() || !info.isDir() || isLinkOrReparsePoint(candidate)) {
            candidate = info.absolutePath();
        }
        if (!isPathInside(candidate, rootPath)
            || isExcludedPath(rootPath, candidate, projectDatabasePath)
            || isLinkOrReparsePoint(candidate)) {
            continue;
        }
        candidates.insert(normalizedAbsolutePath(candidate));
    }

    auto directories = candidates.values();
    std::sort(directories.begin(), directories.end(), [](const QString &left, const QString &right) {
        if (left.size() != right.size()) {
            return left.size() < right.size();
        }
        return comparisonPath(left) < comparisonPath(right);
    });
    return directories;
}
