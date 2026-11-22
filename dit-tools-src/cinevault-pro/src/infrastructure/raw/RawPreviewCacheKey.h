#pragma once

#include <QString>

struct RawPreviewCacheIdentity {
    qint64 sourceSize = 0;
    qint64 sourceModifiedMs = 0;
    int maxEdge = 480;
    QString decoderPackageVersion;
    QString profileVersion;
    QString generatorProfile;
    QString cacheKey;
    QString outputPath;
};

class RawPreviewCacheKey final {
public:
    static QString decoderPackageVersion();
    static QString profileVersion();
    static QString generatorProfile();
    static RawPreviewCacheIdentity fromSource(const QString &sourcePath,
                                              const QString &baseCachePath,
                                              int maxEdge = 480);
};
