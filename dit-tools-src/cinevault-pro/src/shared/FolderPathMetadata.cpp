#include "shared/FolderPathMetadata.h"

#include <QDate>
#include <QDir>
#include <QRegularExpression>
#include <QUrl>

namespace {
QString portableCleanPath(QString path)
{
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.isEmpty()) {
        return {};
    }

    const bool uncPath = path.startsWith(QStringLiteral("//"));
    const bool driveAbsolute = path.size() >= 3
        && path.at(1) == QLatin1Char(':')
        && path.at(2) == QLatin1Char('/');
    const bool rootedPath = !uncPath && !driveAbsolute && path.startsWith(QLatin1Char('/'));

    const auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList cleaned;
    const int protectedParts = uncPath ? 2 : (driveAbsolute ? 1 : 0);
    for (const auto &part : parts) {
        if (part == QStringLiteral(".")) {
            continue;
        }
        if (part == QStringLiteral("..")) {
            if (cleaned.size() > protectedParts && cleaned.constLast() != QStringLiteral("..")) {
                cleaned.removeLast();
            } else if (!uncPath && !driveAbsolute && !rootedPath) {
                cleaned.append(part);
            }
            continue;
        }
        cleaned.append(part);
    }

    if (uncPath) {
        return QStringLiteral("//") + cleaned.join(QLatin1Char('/'));
    }
    if (driveAbsolute) {
        const auto joined = cleaned.join(QLatin1Char('/'));
        return cleaned.size() == 1 ? joined + QLatin1Char('/') : joined;
    }
    if (rootedPath) {
        return QStringLiteral("/") + cleaned.join(QLatin1Char('/'));
    }
    return cleaned.join(QLatin1Char('/'));
}

QDate dateFromFolderName(const QString &name)
{
    static const QRegularExpression separated(
        QStringLiteral(R"((?<!\d)((?:19|20)\d{2})[-_.年](\d{1,2})[-_.月](\d{1,2})(?:日)?(?!\d))"));
    static const QRegularExpression compact(
        QStringLiteral(R"((?<!\d)((?:19|20)\d{2})(\d{2})(\d{2})(?!\d))"));

    for (const auto *expression : {&separated, &compact}) {
        const auto match = expression->match(name);
        if (!match.hasMatch()) {
            continue;
        }
        const QDate candidate(match.captured(1).toInt(),
                              match.captured(2).toInt(),
                              match.captured(3).toInt());
        if (candidate.isValid()) {
            return candidate;
        }
    }
    return {};
}
}

QString FolderPathMetadata::normalizeRelativePath(const QString &path)
{
    const auto normalized = portableCleanPath(path);
    return normalized.isEmpty() || normalized == QStringLiteral(".") ? QStringLiteral("") : normalized;
}

QString FolderPathMetadata::normalizeSourcePath(const QString &path)
{
    auto normalized = path.trimmed();
    if (normalized.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl fileUrl(normalized);
        if (fileUrl.isLocalFile()) {
            normalized = fileUrl.toLocalFile();
        }
    }
    return portableCleanPath(normalized);
}

QString FolderPathMetadata::normalizedPathKey(const QString &path)
{
    return portableCleanPath(path).toCaseFolded();
}

QString FolderPathMetadata::relativePathFromRoot(const QString &rootPath, const QString &absolutePath)
{
    const auto root = portableCleanPath(rootPath);
    const auto child = portableCleanPath(absolutePath);
    if (root.compare(child, Qt::CaseInsensitive) == 0) {
        return QStringLiteral("");
    }

    const auto prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (child.startsWith(prefix, Qt::CaseInsensitive)) {
        return normalizeRelativePath(child.mid(prefix.size()));
    }

    return normalizeRelativePath(QDir(rootPath).relativeFilePath(absolutePath));
}

QString FolderPathMetadata::folderName(const QString &absolutePath, const QString &fallbackName)
{
    auto normalized = portableCleanPath(absolutePath);
    if (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    const auto separator = normalized.lastIndexOf(QLatin1Char('/'));
    const auto name = separator >= 0 ? normalized.mid(separator + 1) : normalized;
    return name.trimmed().isEmpty() ? fallbackName : name;
}

QString FolderPathMetadata::parentRelativePath(const QString &relativePath)
{
    const auto normalized = normalizeRelativePath(relativePath);
    const auto separator = normalized.lastIndexOf(QLatin1Char('/'));
    return separator < 0 ? QStringLiteral("") : normalized.left(separator);
}

int FolderPathMetadata::depth(const QString &relativePath)
{
    const auto normalized = normalizeRelativePath(relativePath);
    return normalized.isEmpty() ? 0 : normalized.count(QLatin1Char('/')) + 1;
}

QStringList FolderPathMetadata::ancestorRelativePaths(const QString &relativePath)
{
    QStringList ancestors;
    auto current = normalizeRelativePath(relativePath);
    while (!current.isEmpty()) {
        ancestors.append(current);
        current = parentRelativePath(current);
    }
    ancestors.append(QString());
    return ancestors;
}

FolderDateMetadata FolderPathMetadata::inferDate(const QString &rootFolderName, const QString &relativePath)
{
    const auto normalized = normalizeRelativePath(relativePath);
    const auto relativeParts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    QStringList names;
    QStringList anchors;
    names.append(rootFolderName);
    anchors.append(QStringLiteral("."));

    QString anchor;
    for (const auto &part : relativeParts) {
        anchor = anchor.isEmpty() ? part : anchor + QLatin1Char('/') + part;
        names.append(part);
        anchors.append(anchor);
    }

    for (int index = names.size() - 1; index >= 0; --index) {
        const auto date = dateFromFolderName(names.at(index));
        if (date.isValid()) {
            return {date.toString(Qt::ISODate), anchors.at(index)};
        }
    }
    return {QStringLiteral(""), QStringLiteral("")};
}

QString FolderPathMetadata::globalFolderKey(const QString &projectUuid,
                                            qint64 sourceRootId,
                                            const QString &relativePath)
{
    return QStringLiteral("%1|%2|%3")
        .arg(projectUuid, QString::number(sourceRootId), normalizeRelativePath(relativePath).toCaseFolded());
}
