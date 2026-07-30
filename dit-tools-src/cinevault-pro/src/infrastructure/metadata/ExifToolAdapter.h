#pragma once

#include "domain/Entities.h"

#include <QString>
#include <QVector>

#include <optional>

struct EmbeddedMetadataResult {
    qint64 assetId = 0;
    ProbeStatus status = ProbeStatus::Pending;
    qint64 fingerprintSize = 0;
    QString fingerprintModified;
    QString toolVersion;
    QString captureTime;
    QString createTime;
    QString cameraMake;
    QString cameraModel;
    QString lensModel;
    QString cameraSerialHash;
    std::optional<double> gpsLatitude;
    std::optional<double> gpsLongitude;
    std::optional<double> gpsAltitude;
    int orientation = 0;
    int width = 0;
    int height = 0;
    qint64 durationMs = 0;
    double frameRate = 0;
    QString videoCodec;
    QString colorSpace;
    int sampleRate = 0;
    int channels = 0;
    qint64 bitRate = 0;
    QString timecode;
    QString title;
    QString description;
    QString artist;
    QString album;
    QString genre;
    QString keywords;
    QString searchText;
    QString rawJson;
    QString errorMessage;
};

class ExifToolAdapter {
public:
    ExifToolAdapter();

    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString unavailableReason() const;
    [[nodiscard]] QString executablePath() const;
    [[nodiscard]] QString version() const;

    QVector<EmbeddedMetadataResult> extract(const QVector<AssetFile> &assets) const;
    QVector<EmbeddedMetadataResult> extract(const QVector<AssetFile> &assets, int timeoutMs) const;

private:
    QString m_executablePath;
    QString m_version;
    QString m_unavailableReason;
};
