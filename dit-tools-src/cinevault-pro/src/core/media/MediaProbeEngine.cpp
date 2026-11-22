#include "core/media/MediaProbeEngine.h"

#include "infrastructure/ffmpeg/FFmpegAdapter.h"

MediaProbeEngine::MediaProbeEngine(FFmpegAdapter *adapter, QObject *parent)
    : QObject(parent)
    , m_adapter(adapter)
{
}

bool MediaProbeEngine::isAvailable() const
{
    return m_adapter && m_adapter->isAvailable();
}

QString MediaProbeEngine::statusMessage() const
{
    return isAvailable()
        ? QStringLiteral("媒体探测模块可用")
        : (m_adapter ? m_adapter->unavailableReason() : QStringLiteral("媒体探测模块未初始化"));
}

MediaProbeResult MediaProbeEngine::probe(const AssetFile &asset) const
{
    if (!m_adapter) {
        MediaProbeResult result;
        result.assetId = asset.id;
        result.status = ProbeStatus::Unavailable;
        result.errorMessage = QStringLiteral("媒体探测模块未初始化");
        return result;
    }
    return m_adapter->probe(asset);
}
