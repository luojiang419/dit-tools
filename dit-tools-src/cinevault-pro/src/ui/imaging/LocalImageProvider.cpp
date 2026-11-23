#include "ui/imaging/LocalImageProvider.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QUrl>

#if defined(CINEVAULT_HAS_BUNDLED_WEBP) && CINEVAULT_HAS_BUNDLED_WEBP
#include <webp/decode.h>
#endif

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shobjidl_core.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace {
bool isWebpFile(const QString &localPath)
{
    return QFileInfo(localPath).suffix().compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0;
}

QString resolveLocalPath(const QString &id)
{
    const auto decoded = QUrl::fromPercentEncoding(id.toUtf8());
    const QUrl url(decoded);
    if (url.isLocalFile()) {
        return QDir::cleanPath(url.toLocalFile());
    }
    return QDir::cleanPath(decoded);
}

QSize normalizedRequestedSize(const QSize &requestedSize)
{
    if (!requestedSize.isValid()) {
        return QSize(2048, 2048);
    }
    return requestedSize.expandedTo(QSize(64, 64));
}

QImage loadWithQt(const QString &localPath, const QSize &requestedSize)
{
    QImageReader reader(localPath);
    reader.setAutoTransform(true);
    auto image = reader.read();
    if (image.isNull()) {
        return {};
    }
    if (requestedSize.isValid() && (image.size().width() > requestedSize.width() || image.size().height() > requestedSize.height())) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

#if defined(CINEVAULT_HAS_BUNDLED_WEBP) && CINEVAULT_HAS_BUNDLED_WEBP
QImage loadWithBundledWebpDecoder(const QString &localPath, const QSize &requestedSize)
{
    if (!isWebpFile(localPath)) {
        return {};
    }

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const auto encodedBytes = file.readAll();
    if (encodedBytes.isEmpty()) {
        return {};
    }

    int width = 0;
    int height = 0;
    if (WebPGetInfo(reinterpret_cast<const uint8_t *>(encodedBytes.constData()),
                    static_cast<size_t>(encodedBytes.size()),
                    &width,
                    &height) == 0
        || width <= 0
        || height <= 0) {
        return {};
    }

    QImage image(width, height, QImage::Format_RGBA8888);
    if (image.isNull()) {
        return {};
    }

    const auto *decoded = WebPDecodeRGBAInto(
        reinterpret_cast<const uint8_t *>(encodedBytes.constData()),
        static_cast<size_t>(encodedBytes.size()),
        image.bits(),
        static_cast<size_t>(image.sizeInBytes()),
        image.bytesPerLine());
    if (!decoded) {
        return {};
    }

    if (requestedSize.isValid()
        && (image.width() > requestedSize.width() || image.height() > requestedSize.height())) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}
#endif

#ifdef Q_OS_WIN
QImage imageFromBitmap(HBITMAP bitmapHandle)
{
    if (!bitmapHandle) {
        return {};
    }

    BITMAP bitmap{};
    if (GetObjectW(bitmapHandle, sizeof(bitmap), &bitmap) == 0 || bitmap.bmWidth <= 0 || bitmap.bmHeight == 0) {
        return {};
    }

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = bitmap.bmWidth;
    header.biHeight = -qAbs(bitmap.bmHeight);
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;

    QImage image(bitmap.bmWidth, qAbs(bitmap.bmHeight), QImage::Format_ARGB32);
    if (image.isNull()) {
        return {};
    }

    HDC dc = GetDC(nullptr);
    const auto copied = GetDIBits(dc,
                                  bitmapHandle,
                                  0,
                                  static_cast<UINT>(image.height()),
                                  image.bits(),
                                  reinterpret_cast<BITMAPINFO *>(&header),
                                  DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (copied == 0) {
        return {};
    }
    return image;
}

QImage loadWithWindowsShell(const QString &localPath, const QSize &requestedSize)
{
    const auto nativePath = QDir::toNativeSeparators(localPath);
    if (nativePath.isEmpty()) {
        return {};
    }

    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initResult);

    Microsoft::WRL::ComPtr<IShellItemImageFactory> imageFactory;
    const auto createResult = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                                          nullptr,
                                                          IID_PPV_ARGS(&imageFactory));
    if (FAILED(createResult) || !imageFactory) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {};
    }

    const auto size = normalizedRequestedSize(requestedSize);
    SIZE nativeSize{ qMax(1, size.width()), qMax(1, size.height()) };
    HBITMAP bitmapHandle = nullptr;
    const auto imageResult = imageFactory->GetImage(nativeSize,
                                                    SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT,
                                                    &bitmapHandle);
    QImage image;
    if (SUCCEEDED(imageResult) && bitmapHandle) {
        image = imageFromBitmap(bitmapHandle);
        DeleteObject(bitmapHandle);
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return image;
}
#endif
}

LocalImageProvider::LocalImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage LocalImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const auto localPath = resolveLocalPath(id);
    auto image = loadWithQt(localPath, requestedSize);
#if defined(CINEVAULT_HAS_BUNDLED_WEBP) && CINEVAULT_HAS_BUNDLED_WEBP
    if (image.isNull()) {
        image = loadWithBundledWebpDecoder(localPath, requestedSize);
    }
#endif
#ifdef Q_OS_WIN
    if (image.isNull()) {
        image = loadWithWindowsShell(localPath, requestedSize);
    }
#endif
    if (size) {
        *size = image.size();
    }
    return image;
}
