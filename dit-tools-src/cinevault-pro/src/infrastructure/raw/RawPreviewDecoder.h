#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct RawPreviewDecodeResult {
    bool success = false;
    QString outputPath;
    QString provider;
    bool placeholder = false;
    int width = 0;
    int height = 0;
    qint64 sourceSize = 0;
    qint64 sourceModifiedMs = 0;
    QString decoderPackageVersion;
    QString profileVersion;
    QString generatorProfile;
    QString cacheKey;
    QString errorCode;
    QString errorMessage;
    QJsonArray attempts;
};

class RawPreviewDecoder final {
public:
    static RawPreviewDecodeResult decode(const QJsonObject &payload);
};
