#pragma once

#include "domain/Entities.h"

#include <QObject>

class FFmpegAdapter;

class MediaProbeEngine : public QObject {
    Q_OBJECT

public:
    explicit MediaProbeEngine(FFmpegAdapter *adapter, QObject *parent = nullptr);

    bool isAvailable() const;
    QString statusMessage() const;
    MediaProbeResult probe(const AssetFile &asset) const;

private:
    FFmpegAdapter *m_adapter = nullptr;
};
