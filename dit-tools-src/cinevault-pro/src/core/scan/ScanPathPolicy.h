#pragma once

#include <QString>
#include <QStringList>

class ScanPathPolicy final {
public:
    static bool isPathInside(const QString &candidatePath, const QString &rootPath);
    static bool pathsOverlap(const QString &firstPath, const QString &secondPath);
    static bool isLinkOrReparsePoint(const QString &path);
    static bool isExcludedPath(const QString &sourceRootPath,
                               const QString &candidatePath,
                               const QString &projectDatabasePath);
    static QStringList normalizeDirtyDirectories(const QString &sourceRootPath,
                                                 const QStringList &changedPaths,
                                                 const QString &projectDatabasePath);
};
