#include "infrastructure/raw/RawPreviewCacheKey.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

QString RawPreviewCacheKey::decoderPackageVersion()
{
    return QStringLiteral("libraw-0.22.2+gpr-sdk-446c736");
}

QString RawPreviewCacheKey::profileVersion()
{
    return QStringLiteral("raw-preview-v1");
}

QString RawPreviewCacheKey::generatorProfile()
{
    return QStringLiteral("%1/%2").arg(profileVersion(), decoderPackageVersion());
}

RawPreviewCacheIdentity RawPreviewCacheKey::fromSource(const QString &sourcePath,
                                                       const QString &baseCachePath,
                                                       int maxEdge)
{
    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo baseCacheInfo(baseCachePath);
    RawPreviewCacheIdentity identity;
    identity.sourceSize = sourceInfo.isFile() ? sourceInfo.size() : 0;
    identity.sourceModifiedMs = sourceInfo.lastModified().isValid()
        ? sourceInfo.lastModified().toUTC().toMSecsSinceEpoch()
        : 0;
    identity.maxEdge = qBound(32, maxEdge, 480);
    identity.decoderPackageVersion = decoderPackageVersion();
    identity.profileVersion = profileVersion();
    identity.generatorProfile = generatorProfile();

    const auto keyPayload = QStringLiteral(
        "size=%1\nmtime_ms=%2\ndecoder=%3\nprofile=%4\nmax_edge=%5\n")
                                .arg(identity.sourceSize)
                                .arg(identity.sourceModifiedMs)
                                .arg(identity.decoderPackageVersion,
                                     identity.profileVersion)
                                .arg(identity.maxEdge)
                                .toUtf8();
    identity.cacheKey = QString::fromLatin1(
        QCryptographicHash::hash(keyPayload, QCryptographicHash::Sha256).toHex());
    identity.outputPath = QDir(baseCacheInfo.absolutePath())
                              .filePath(QStringLiteral("%1.raw-%2.jpg")
                                            .arg(baseCacheInfo.completeBaseName(),
                                                 identity.cacheKey.left(24)));
    return identity;
}
