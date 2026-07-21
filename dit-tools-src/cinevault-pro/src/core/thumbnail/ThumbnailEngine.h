#pragma once

#include "domain/Entities.h"

#include <QObject>

#include <memory>

class FFmpegAdapter;
class AppSettings;
class RawWorkerClient;

class ThumbnailEngine : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailEngine(FFmpegAdapter *adapter, AppSettings *settings, QObject *parent = nullptr);
    ~ThumbnailEngine() override;

    bool isAvailable() const;
    QString statusMessage() const;
    virtual ThumbnailResult createPlaceholder(const ThumbnailRequest &request) const;

private:
    FFmpegAdapter *m_adapter = nullptr;
    AppSettings *m_settings = nullptr;
    mutable std::unique_ptr<RawWorkerClient> m_rawWorkerClient;
};
