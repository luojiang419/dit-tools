#include "shared/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString ensureDir(const QString &path, QString *errorMessage)
{
    QDir dir;
    if (!dir.mkpath(path)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建目录：%1").arg(path);
        }
        return {};
    }
    return path;
}

QString sanitizedVideoKey(QString videoKey)
{
    videoKey.replace(QLatin1Char(':'), QLatin1Char('_'));
    videoKey.replace(QLatin1Char('/'), QLatin1Char('_'));
    videoKey.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return videoKey;
}
}

QString Paths::appDataRoot()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QDir::homePath() + QStringLiteral("/影资管家") : base;
}

QString Paths::installRoot()
{
    return QCoreApplication::applicationDirPath();
}

QString Paths::installDataRoot()
{
    return QDir(installRoot()).filePath(QStringLiteral("data"));
}

QString Paths::fallbackDataRoot()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty()
        ? QDir::homePath() + QStringLiteral("/影资管家/data")
        : QDir(base).filePath(QStringLiteral("data"));
}

QString Paths::resolvedDataRoot()
{
    // Mutable user data must never live below the application installation
    // directory. Installers own that tree and may replace or remove its files
    // during an upgrade. Runtime assets such as the bundled BGE model continue
    // to be read directly from installDataRoot().
    return fallbackDataRoot();
}

QString Paths::frameCacheRoot()
{
    return QDir(resolvedDataRoot()).filePath(QStringLiteral("frame-cache"));
}

QString Paths::semanticSearchRoot()
{
    return QDir(resolvedDataRoot()).filePath(QStringLiteral("semantic-search"));
}

QString Paths::semanticSearchIndexPath()
{
    return QDir(semanticSearchRoot()).filePath(QStringLiteral("materials-v1.usearch"));
}

QString Paths::frameCacheDirectory(const QString &videoKey)
{
    return QDir(frameCacheRoot()).filePath(sanitizedVideoKey(videoKey));
}

bool Paths::clearFrameCache(QString *errorMessage)
{
    QDir dir(frameCacheRoot());
    if (!dir.exists()) {
        return true;
    }
    if (!dir.removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法清理解析缓存：%1").arg(dir.absolutePath());
        }
        return false;
    }
    return !ensureDir(frameCacheRoot(), errorMessage).isEmpty();
}

QString Paths::projectRootFromDatabasePath(const QString &databasePath)
{
    const QFileInfo info(databasePath.trimmed());
    if (info.isDir()) {
        return info.absoluteFilePath();
    }
    return info.absolutePath();
}

QString Paths::projectThumbnailCachePath(const QString &databasePath, qint64 sourceRootId, qint64 assetId)
{
    return QDir(projectThumbnailCacheRoot(databasePath))
        .filePath(QStringLiteral("source_%1/%2.jpg").arg(sourceRootId).arg(assetId));
}

QString Paths::projectThumbnailCacheRoot(const QString &databasePath)
{
    return QDir(projectRootFromDatabasePath(databasePath))
        .filePath(QStringLiteral("cache/thumbnails"));
}

QString Paths::projectReportPreviewRoot(const QString &projectRoot)
{
    return QDir(projectRoot).filePath(QStringLiteral("cache/report-preview"));
}

QString Paths::projectFrameCacheDirectory(const QString &databasePath, const QString &videoKey)
{
    return QDir(projectRootFromDatabasePath(databasePath))
        .filePath(QStringLiteral("analysis/frames/%1").arg(sanitizedVideoKey(videoKey)));
}

QString Paths::projectContactSheetPath(const QString &databasePath, const QString &videoKey, int frameCount)
{
    return QDir(projectFrameCacheDirectory(databasePath, videoKey))
        .filePath(QStringLiteral("contact_sheet_%1.jpg").arg(qMax(1, frameCount)));
}

QString Paths::projectsRoot()
{
    return appDataRoot() + QStringLiteral("/projects");
}

QString Paths::cacheRoot()
{
    return appDataRoot() + QStringLiteral("/cache");
}

QString Paths::updatesRoot()
{
    return QDir(cacheRoot()).filePath(QStringLiteral("updates"));
}

QString Paths::configRoot()
{
    return appDataRoot() + QStringLiteral("/config");
}

QString Paths::logsRoot()
{
    return appDataRoot() + QStringLiteral("/logs");
}

bool Paths::ensureBaseDirectories(QString *errorMessage)
{
    return !ensureDir(projectsRoot(), errorMessage).isEmpty()
        && !ensureDir(cacheRoot(), errorMessage).isEmpty()
        && !ensureDir(updatesRoot(), errorMessage).isEmpty()
        && !ensureDir(configRoot(), errorMessage).isEmpty()
        && !ensureDir(logsRoot(), errorMessage).isEmpty()
        && !ensureDir(resolvedDataRoot(), errorMessage).isEmpty()
        && !ensureDir(frameCacheRoot(), errorMessage).isEmpty()
        && !ensureDir(semanticSearchRoot(), errorMessage).isEmpty();
}
