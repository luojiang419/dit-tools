#pragma once

#include <QString>
#include <QStringList>

struct FolderDateMetadata {
    QString normalizedDate;
    QString anchorRelativePath;
};

namespace FolderPathMetadata {

QString normalizeRelativePath(const QString &path);
QString normalizeSourcePath(const QString &path);
QString normalizedPathKey(const QString &path);
QString relativePathFromRoot(const QString &rootPath, const QString &absolutePath);
QString folderName(const QString &absolutePath, const QString &fallbackName = QString());
QString parentRelativePath(const QString &relativePath);
int depth(const QString &relativePath);
QStringList ancestorRelativePaths(const QString &relativePath);
FolderDateMetadata inferDate(const QString &rootFolderName, const QString &relativePath);
QString globalFolderKey(const QString &projectUuid, qint64 sourceRootId, const QString &relativePath);

}
