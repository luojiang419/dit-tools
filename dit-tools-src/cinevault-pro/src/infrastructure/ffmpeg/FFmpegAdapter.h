#pragma once

#include "domain/Entities.h"

#include <QString>

class FFmpegAdapter {
public:
    FFmpegAdapter();

    bool isAvailable() const;
    QString unavailableReason() const;
    MediaProbeResult probe(const AssetFile &asset) const;
    FrameExtractionResult extractFrames(const FrameExtractionRequest &request) const;
    ThumbnailResult generateThumbnail(const ThumbnailRequest &request) const;

private:
    bool m_available = false;
    QString m_unavailableReason;
    QString m_ffprobePath;
    QString m_ffmpegPath;
};
