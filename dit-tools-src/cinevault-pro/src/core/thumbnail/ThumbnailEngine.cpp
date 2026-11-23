#include "core/thumbnail/ThumbnailEngine.h"

#include "core/thumbnail/RawFormatRegistry.h"
#include "infrastructure/config/AppSettings.h"
#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include "infrastructure/raw/RawWorkerClient.h"

#include <QFile>
#include <QFileInfo>

namespace {

bool isRawSource(const QString &sourcePath)
{
    if (RawFormatRegistry::isRawFileName(sourcePath)) {
        return true;
    }
    const auto extension = QFileInfo(sourcePath).suffix().toLower();
    if (extension != QStringLiteral("tif")
        && extension != QStringLiteral("tiff")
        && extension != QStringLiteral("bin")) {
        return false;
    }
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return RawFormatRegistry::findBySignature(file.read(64 * 1024)) != nullptr;
}

} // namespace

ThumbnailEngine::ThumbnailEngine(FFmpegAdapter *adapter, AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_adapter(adapter)
    , m_settings(settings)
    , m_rawWorkerClient(std::make_unique<RawWorkerClient>())
{
}

ThumbnailEngine::~ThumbnailEngine() = default;

bool ThumbnailEngine::isAvailable() const
{
    return QFileInfo(RawWorkerClient::defaultExecutablePath()).isFile()
        || (m_adapter && m_adapter->isAvailable());
}

QString ThumbnailEngine::statusMessage() const
{
    return isAvailable()
        ? QStringLiteral("缩略图模块可用")
        : (m_adapter ? m_adapter->unavailableReason() : QStringLiteral("缩略图模块未初始化"));
}

ThumbnailResult ThumbnailEngine::createPlaceholder(const ThumbnailRequest &request) const
{
    if (isRawSource(request.sourcePath)) {
        ThumbnailResult result;
        result.assetId = request.assetId;
        const auto reply = m_rawWorkerClient->decode({
            {QStringLiteral("sourcePath"), request.sourcePath},
            {QStringLiteral("baseCachePath"), request.cachePath},
            {QStringLiteral("maxEdge"), qMin(480, qMax(request.maxWidth, request.maxHeight))},
        });
        result.success = reply.ok;
        result.retryable = reply.retryable;
        result.outputPath = reply.result.value(QStringLiteral("outputPath")).toString();
        result.errorMessage = reply.errorMessage;
        return result;
    }
    if (!m_adapter) {
        ThumbnailResult result;
        result.assetId = request.assetId;
        result.success = false;
        result.errorMessage = QStringLiteral("缩略图模块未初始化");
        return result;
    }
    auto normalizedRequest = request;
    if (normalizedRequest.assetType == AssetType::Image) {
        normalizedRequest.frameIndex = 1;
    } else if (m_settings) {
        normalizedRequest.frameIndex = m_settings->thumbnailFrameIndex();
    }
    return m_adapter->generateThumbnail(normalizedRequest);
}
