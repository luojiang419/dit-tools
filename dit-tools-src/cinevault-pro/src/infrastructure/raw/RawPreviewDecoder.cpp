#include "infrastructure/raw/RawPreviewDecoder.h"

#include "infrastructure/raw/RawPreviewCacheKey.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTransform>

#include <functional>
#include <memory>

#if CINEVAULT_HAS_LIBRAW
#include <libraw/libraw.h>
#endif

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#else
#include <cstdio>
#endif

namespace {

constexpr int MaximumPreviewEdge = 480;
constexpr int JpegQuality = 85;

struct ProviderImage {
    QImage image;
    bool orientationApplied = false;
    int libRawFlip = 0;
};

struct ProcessResult {
    bool ok = false;
    QByteArray output;
    QString errorMessage;
};

QString existingExecutable(const QStringList &candidates)
{
    for (const auto &candidate : candidates) {
        const QFileInfo info(candidate);
        if (!candidate.trimmed().isEmpty() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString environmentPath(const char *name)
{
    return QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv(name)).trimmed());
}

ProcessResult runProcess(const QString &program,
                         const QStringList &arguments,
                         int timeoutMs,
                         bool requireOutput = true)
{
    ProcessResult result;
    if (program.isEmpty()) {
        result.errorMessage = QStringLiteral("运行时不可用");
        return result;
    }
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start();
    if (!process.waitForStarted(3000)) {
        result.errorMessage = process.errorString();
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.errorMessage = QStringLiteral("子进程执行超时");
        return result;
    }
    result.output = process.readAllStandardOutput();
    const auto diagnostic = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.ok = process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0
        && (!requireOutput || !result.output.isEmpty());
    if (!result.ok) {
        result.errorMessage = diagnostic.isEmpty()
            ? QStringLiteral("子进程退出码：%1").arg(process.exitCode())
            : diagnostic.left(500);
    }
    return result;
}

QImage imageFromEncodedBytes(const QByteArray &bytes, bool *orientationApplied, QString *errorMessage)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("无法读取预览数据");
        return {};
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto transformation = reader.transformation();
    auto image = reader.read();
    if (image.isNull()) {
        *errorMessage = QStringLiteral("预览图片解析失败：%1").arg(reader.errorString());
        return {};
    }
    if (orientationApplied) {
        *orientationApplied = transformation != QImageIOHandler::TransformationNone;
    }
    return image;
}

QImage applyLibRawOrientation(QImage image, int flip)
{
    QTransform transform;
    switch (flip) {
    case 3:
    case 180:
        transform.rotate(180);
        break;
    case 5:
    case 270:
        transform.rotate(-90);
        break;
    case 6:
    case 90:
        transform.rotate(90);
        break;
    default:
        return image;
    }
    return image.transformed(transform);
}

QImage normalizedPreview(ProviderImage providerImage, int maxEdge)
{
    auto image = std::move(providerImage.image);
    if (!providerImage.orientationApplied && providerImage.libRawFlip != 0) {
        image = applyLibRawOrientation(std::move(image), providerImage.libRawFlip);
    }
    if (image.isNull()) {
        return {};
    }

    const auto srgb = QColorSpace(QColorSpace::SRgb);
    if (image.colorSpace().isValid() && image.colorSpace() != srgb) {
        auto converted = image.convertedToColorSpace(srgb);
        if (!converted.isNull()) {
            image = std::move(converted);
        }
    }
    image.setColorSpace(srgb);

    const auto boundedEdge = qBound(32, maxEdge, MaximumPreviewEdge);
    auto targetSize = image.size();
    targetSize.scale(boundedEdge, boundedEdge, Qt::KeepAspectRatio);
    if (targetSize != image.size()) {
        image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_RGB888);
}

bool atomicReplace(const QString &temporaryPath, const QString &outputPath, QString *errorMessage)
{
#ifdef Q_OS_WIN
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryPath.utf16()),
                     reinterpret_cast<LPCWSTR>(outputPath.utf16()),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        *errorMessage = QStringLiteral("无法原子替换 RAW 预览缓存，系统错误：%1")
                            .arg(GetLastError());
        return false;
    }
#else
    const auto temporaryName = QFile::encodeName(temporaryPath);
    const auto outputName = QFile::encodeName(outputPath);
    if (std::rename(temporaryName.constData(), outputName.constData()) != 0) {
        *errorMessage = QStringLiteral("无法原子替换 RAW 预览缓存");
        return false;
    }
#endif
    return true;
}

bool writeValidatedJpeg(const QImage &image, const QString &outputPath, QString *errorMessage)
{
    const QFileInfo outputInfo(outputPath);
    if (image.isNull() || outputPath.trimmed().isEmpty()) {
        *errorMessage = QStringLiteral("RAW 预览输出为空");
        return false;
    }
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        *errorMessage = QStringLiteral("无法创建 RAW 预览缓存目录：%1")
                            .arg(outputInfo.absolutePath());
        return false;
    }

    const auto temporaryPath = outputInfo.absoluteFilePath() + QStringLiteral(".tmp");
    QFile::remove(temporaryPath);
    {
        QImageWriter writer(temporaryPath, "jpg");
        writer.setQuality(JpegQuality);
        writer.setOptimizedWrite(true);
        if (!writer.write(image)) {
            *errorMessage = QStringLiteral("RAW 预览 JPEG 写入失败：%1")
                                .arg(writer.errorString());
            QFile::remove(temporaryPath);
            return false;
        }
    }

    {
        QImageReader validator(temporaryPath, "jpg");
        const auto validated = validator.read();
        if (validated.isNull()
            || validated.width() <= 0
            || validated.height() <= 0
            || qMax(validated.width(), validated.height()) > MaximumPreviewEdge) {
            *errorMessage = QStringLiteral("RAW 预览 JPEG 校验失败：%1")
                                .arg(validator.errorString());
            QFile::remove(temporaryPath);
            return false;
        }
    }
    if (!atomicReplace(temporaryPath, outputInfo.absoluteFilePath(), errorMessage)) {
        QFile::remove(temporaryPath);
        return false;
    }
    return true;
}

void addAttempt(QJsonArray *attempts, const QString &provider, const QString &errorMessage)
{
    attempts->append(QJsonObject{
        {QStringLiteral("provider"), provider},
        {QStringLiteral("error"), errorMessage.left(500)},
    });
}

QImage validatedCachedPreview(const QString &path, int maxEdge)
{
    QImageReader reader(path, "jpg");
    const auto image = reader.read();
    if (image.isNull()
        || image.width() <= 0
        || image.height() <= 0
        || qMax(image.width(), image.height()) > qBound(32, maxEdge, MaximumPreviewEdge)) {
        return {};
    }
    return image;
}

#if CINEVAULT_HAS_LIBRAW
QString libRawError(int code)
{
    return QString::fromLatin1(LibRaw::strerror(code));
}

int openLibRaw(LibRaw *processor, const QString &sourcePath)
{
    processor->imgdata.rawparams.max_raw_memory_mb = 512;
#ifdef Q_OS_WIN
    return processor->open_file(reinterpret_cast<const wchar_t *>(sourcePath.utf16()));
#else
    const auto encoded = QFile::encodeName(sourcePath);
    return processor->open_file(encoded.constData());
#endif
}

ProviderImage decodeLibRawEmbedded(const QString &sourcePath, QString *errorMessage)
{
    if (QFileInfo(sourcePath).size() < 64) {
        *errorMessage = QStringLiteral("文件过小，不是有效的 RAW 容器");
        return {};
    }
    auto processor = std::make_unique<LibRaw>();
    auto code = openLibRaw(processor.get(), sourcePath);
    if (code != LIBRAW_SUCCESS) {
        *errorMessage = libRawError(code);
        return {};
    }
    const auto flip = processor->imgdata.sizes.flip;
    code = processor->unpack_thumb();
    if (code != LIBRAW_SUCCESS) {
        *errorMessage = libRawError(code);
        return {};
    }
    int memoryError = LIBRAW_SUCCESS;
    auto *memoryImage = processor->dcraw_make_mem_thumb(&memoryError);
    if (!memoryImage || memoryError != LIBRAW_SUCCESS) {
        *errorMessage = libRawError(memoryError);
        if (memoryImage) {
            LibRaw::dcraw_clear_mem(memoryImage);
        }
        return {};
    }

    ProviderImage result;
    result.libRawFlip = flip;
    if (memoryImage->type == LIBRAW_IMAGE_JPEG) {
        const QByteArray bytes(reinterpret_cast<const char *>(memoryImage->data),
                               static_cast<qsizetype>(memoryImage->data_size));
        result.image = imageFromEncodedBytes(bytes, &result.orientationApplied, errorMessage);
    } else if (memoryImage->type == LIBRAW_IMAGE_BITMAP
               && memoryImage->bits == 8
               && (memoryImage->colors == 3 || memoryImage->colors == 4)) {
        const auto format = memoryImage->colors == 3
            ? QImage::Format_RGB888
            : QImage::Format_RGBA8888;
        result.image = QImage(memoryImage->data,
                              memoryImage->width,
                              memoryImage->height,
                              memoryImage->width * memoryImage->colors,
                              format)
                           .copy();
    } else {
        *errorMessage = QStringLiteral("LibRaw 内嵌预览格式不受支持：%1")
                            .arg(static_cast<int>(memoryImage->type));
    }
    LibRaw::dcraw_clear_mem(memoryImage);
    return result;
}

ProviderImage decodeLibRawRendered(const QString &sourcePath, QString *errorMessage)
{
    if (QFileInfo(sourcePath).size() < 64) {
        *errorMessage = QStringLiteral("文件过小，不是有效的 RAW 容器");
        return {};
    }
    auto processor = std::make_unique<LibRaw>();
    processor->imgdata.params.output_bps = 8;
    processor->imgdata.params.output_color = 1;
    processor->imgdata.params.use_camera_wb = 1;
    processor->imgdata.params.half_size = 1;
    auto code = openLibRaw(processor.get(), sourcePath);
    if (code == LIBRAW_SUCCESS) {
        code = processor->unpack();
    }
    if (code == LIBRAW_SUCCESS) {
        code = processor->dcraw_process();
    }
    if (code != LIBRAW_SUCCESS) {
        *errorMessage = libRawError(code);
        return {};
    }

    int memoryError = LIBRAW_SUCCESS;
    auto *memoryImage = processor->dcraw_make_mem_image(&memoryError);
    if (!memoryImage || memoryError != LIBRAW_SUCCESS) {
        *errorMessage = libRawError(memoryError);
        if (memoryImage) {
            LibRaw::dcraw_clear_mem(memoryImage);
        }
        return {};
    }
    ProviderImage result;
    result.orientationApplied = true;
    if (memoryImage->type == LIBRAW_IMAGE_BITMAP
        && memoryImage->bits == 8
        && (memoryImage->colors == 3 || memoryImage->colors == 4)) {
        const auto format = memoryImage->colors == 3
            ? QImage::Format_RGB888
            : QImage::Format_RGBA8888;
        result.image = QImage(memoryImage->data,
                              memoryImage->width,
                              memoryImage->height,
                              memoryImage->width * memoryImage->colors,
                              format)
                           .copy();
    } else {
        *errorMessage = QStringLiteral("LibRaw 显影输出格式不受支持");
    }
    LibRaw::dcraw_clear_mem(memoryImage);
    return result;
}
#else
ProviderImage decodeLibRawEmbedded(const QString &, QString *errorMessage)
{
    *errorMessage = QStringLiteral("构建未包含 LibRaw");
    return {};
}

ProviderImage decodeLibRawRendered(const QString &, QString *errorMessage)
{
    *errorMessage = QStringLiteral("构建未包含 LibRaw");
    return {};
}
#endif

ProviderImage decodeGprSdk(const QString &sourcePath, QString *errorMessage)
{
    if (QFileInfo(sourcePath).suffix().compare(QStringLiteral("gpr"), Qt::CaseInsensitive) != 0) {
        *errorMessage = QStringLiteral("GoPro GPR SDK 仅处理 GPR 文件");
        return {};
    }
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto gprTools = existingExecutable({
        environmentPath("CINEVAULT_GPR_TOOLS_PATH"),
        QDir(appDir).filePath(QStringLiteral("gpr/gpr_tools.exe")),
        QDir(appDir).filePath(QStringLiteral("gpr_tools.exe")),
        QStandardPaths::findExecutable(QStringLiteral("gpr_tools.exe")),
        QStandardPaths::findExecutable(QStringLiteral("gpr_tools")),
    });
    if (gprTools.isEmpty()) {
        *errorMessage = QStringLiteral("未找到 GoPro GPR SDK 运行时");
        return {};
    }

    QTemporaryDir temporaryDir(QDir::tempPath() + QStringLiteral("/CineVaultGpr-XXXXXX"));
    if (!temporaryDir.isValid()) {
        *errorMessage = QStringLiteral("无法创建 GPR 解码临时目录");
        return {};
    }
    const auto outputPath = temporaryDir.filePath(QStringLiteral("preview.jpg"));
    const auto process = runProcess(gprTools, {
        QStringLiteral("-i"), sourcePath,
        QStringLiteral("-o"), outputPath,
        QStringLiteral("-r"), QStringLiteral("8:1"),
    }, 10000, false);
    if (!process.ok) {
        *errorMessage = process.errorMessage;
        return {};
    }

    const QFileInfo outputInfo(outputPath);
    constexpr qint64 MaximumGprPreviewBytes = 64 * 1024 * 1024;
    if (!outputInfo.isFile()
        || outputInfo.size() <= 0
        || outputInfo.size() > MaximumGprPreviewBytes) {
        *errorMessage = QStringLiteral("GoPro GPR SDK 未生成有效预览文件");
        return {};
    }
    QFile output(outputPath);
    if (!output.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("无法读取 GoPro GPR SDK 预览：%1")
                            .arg(output.errorString());
        return {};
    }
    ProviderImage result;
    result.image = imageFromEncodedBytes(output.readAll(),
                                         &result.orientationApplied,
                                         errorMessage);
    return result;
}

ProviderImage decodeExifToolPreview(const QString &sourcePath, QString *errorMessage)
{
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto exifTool = existingExecutable({
        environmentPath("CINEVAULT_EXIFTOOL_PATH"),
        QDir(appDir).filePath(QStringLiteral("exiftool/exiftool.exe")),
        QDir(appDir).filePath(QStringLiteral("exiftool.exe")),
        QStandardPaths::findExecutable(QStringLiteral("exiftool.exe")),
        QStandardPaths::findExecutable(QStringLiteral("exiftool")),
    });
    if (exifTool.isEmpty()) {
        *errorMessage = QStringLiteral("未找到 ExifTool");
        return {};
    }

    QStringList errors;
    for (const auto &tag : {QStringLiteral("JpgFromRaw"),
                            QStringLiteral("PreviewImage"),
                            QStringLiteral("ThumbnailImage")}) {
        const auto process = runProcess(exifTool, {
            QStringLiteral("-b"),
            QStringLiteral("-%1").arg(tag),
            QStringLiteral("-charset"), QStringLiteral("filename=UTF8"),
            sourcePath,
        }, 8000);
        if (!process.ok) {
            errors.append(QStringLiteral("%1: %2").arg(tag, process.errorMessage));
            continue;
        }
        ProviderImage result;
        result.image = imageFromEncodedBytes(process.output,
                                             &result.orientationApplied,
                                             errorMessage);
        if (!result.image.isNull()) {
            return result;
        }
        errors.append(QStringLiteral("%1: %2").arg(tag, *errorMessage));
    }
    *errorMessage = errors.join(QStringLiteral("；"));
    return {};
}

#ifdef Q_OS_WIN
ProviderImage decodeWic(const QString &sourcePath, int maxEdge, QString *errorMessage)
{
    static const auto initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
        *errorMessage = QStringLiteral("WIC COM 初始化失败：0x%1")
                            .arg(static_cast<quint32>(initializeResult), 8, 16, QLatin1Char('0'));
        return {};
    }

    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    auto result = CoCreateInstance(CLSID_WICImagingFactory,
                                   nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(result)) {
        result = factory->CreateDecoderFromFilename(
            reinterpret_cast<LPCWSTR>(sourcePath.utf16()),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder);
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(result)) {
        result = decoder->GetFrame(0, &frame);
    }
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(result)) {
        result = frame->GetSize(&width, &height);
    }
    if (FAILED(result) || width == 0 || height == 0) {
        *errorMessage = QStringLiteral("WIC 无法读取图片：0x%1")
                            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
        return {};
    }

    const auto scale = qMin(1.0, static_cast<double>(qBound(32, maxEdge, MaximumPreviewEdge))
                                     / static_cast<double>(qMax(width, height)));
    const auto targetWidth = qMax<UINT>(1, static_cast<UINT>(width * scale));
    const auto targetHeight = qMax<UINT>(1, static_cast<UINT>(height * scale));
    ComPtr<IWICBitmapScaler> scaler;
    result = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(result)) {
        result = scaler->Initialize(frame.Get(),
                                    targetWidth,
                                    targetHeight,
                                    WICBitmapInterpolationModeFant);
    }
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(result)) {
        result = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(result)) {
        result = converter->Initialize(scaler.Get(),
                                       GUID_WICPixelFormat32bppBGRA,
                                       WICBitmapDitherTypeNone,
                                       nullptr,
                                       0.0,
                                       WICBitmapPaletteTypeCustom);
    }

    QImage image(static_cast<int>(targetWidth),
                 static_cast<int>(targetHeight),
                 QImage::Format_ARGB32);
    if (SUCCEEDED(result)) {
        result = converter->CopyPixels(nullptr,
                                       static_cast<UINT>(image.bytesPerLine()),
                                       static_cast<UINT>(image.sizeInBytes()),
                                       image.bits());
    }
    if (FAILED(result)) {
        *errorMessage = QStringLiteral("WIC 像素转换失败：0x%1")
                            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
        return {};
    }
    return {image, true, 0};
}
#else
ProviderImage decodeWic(const QString &, int, QString *errorMessage)
{
    *errorMessage = QStringLiteral("WIC 仅在 Windows 上可用");
    return {};
}
#endif

ProviderImage decodeFfmpeg(const QString &sourcePath, int maxEdge, QString *errorMessage)
{
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto ffmpegRoot = environmentPath("CINEVAULT_FFMPEG_ROOT");
    const auto ffmpegBin = environmentPath("CINEVAULT_FFMPEG_BIN");
    const auto ffmpeg = existingExecutable({
        environmentPath("CINEVAULT_FFMPEG_PATH"),
        QDir(ffmpegBin).filePath(QStringLiteral("ffmpeg.exe")),
        QDir(ffmpegRoot).filePath(QStringLiteral("bin/ffmpeg.exe")),
        QDir(appDir).filePath(QStringLiteral("ffmpeg/bin/ffmpeg.exe")),
        QDir(appDir).filePath(QStringLiteral("ffmpeg.exe")),
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg.exe")),
        QStandardPaths::findExecutable(QStringLiteral("ffmpeg")),
    });
    if (ffmpeg.isEmpty()) {
        *errorMessage = QStringLiteral("未找到 FFmpeg");
        return {};
    }
    const auto edge = qBound(32, maxEdge, MaximumPreviewEdge);
    const auto filter = QStringLiteral(
        "scale='min(%1,iw)':'min(%1,ih)':force_original_aspect_ratio=decrease")
                            .arg(edge);
    const auto process = runProcess(ffmpeg, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), sourcePath,
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vf"), filter,
        QStringLiteral("-f"), QStringLiteral("image2pipe"),
        QStringLiteral("-vcodec"), QStringLiteral("mjpeg"),
        QStringLiteral("pipe:1"),
    }, 30000);
    if (!process.ok) {
        *errorMessage = process.errorMessage;
        return {};
    }
    ProviderImage result;
    result.image = imageFromEncodedBytes(process.output, &result.orientationApplied, errorMessage);
    return result;
}

ProviderImage rawPlaceholder(const QString &sourcePath, int maxEdge)
{
    const auto width = qBound(96, maxEdge, MaximumPreviewEdge);
    const auto height = qMax(64, (width * 2) / 3);
    QImage image(width, height, QImage::Format_RGB888);
    image.fill(QColor(QStringLiteral("#20242a")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#64748b")), 2));
    painter.setBrush(QColor(QStringLiteral("#2d333b")));
    painter.drawRoundedRect(image.rect().adjusted(20, 20, -20, -20), 12, 12);
    auto font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(24, width / 8));
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#e2e8f0")));
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("RAW"));
    font.setBold(false);
    font.setPixelSize(qMax(12, width / 28));
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#94a3b8")));
    painter.drawText(image.rect().adjusted(0, height / 2, 0, -20),
                     Qt::AlignHCenter | Qt::AlignBottom,
                     QFileInfo(sourcePath).suffix().toUpper());
    return {image, true, 0};
}

} // namespace

RawPreviewDecodeResult RawPreviewDecoder::decode(const QJsonObject &payload)
{
    RawPreviewDecodeResult result;
    const auto sourcePath = QDir::fromNativeSeparators(
        payload.value(QStringLiteral("sourcePath")).toString().trimmed());
    const auto baseCachePath = QDir::fromNativeSeparators(
        payload.value(QStringLiteral("baseCachePath"))
            .toString(payload.value(QStringLiteral("outputPath")).toString())
            .trimmed());
    const auto maxEdge = qBound(32,
                                payload.value(QStringLiteral("maxEdge")).toInt(MaximumPreviewEdge),
                                MaximumPreviewEdge);
    const auto isGpr = QFileInfo(sourcePath).suffix().compare(
        QStringLiteral("gpr"), Qt::CaseInsensitive) == 0;
    const auto providerStartIndex = qBound(
        0, payload.value(QStringLiteral("providerStartIndex")).toInt(), isGpr ? 6 : 5);
    if (!QFileInfo(sourcePath).isFile()) {
        result.errorCode = QStringLiteral("source_missing");
        result.errorMessage = QStringLiteral("RAW 源文件不存在：%1").arg(sourcePath);
        return result;
    }
    if (baseCachePath.isEmpty()) {
        result.errorCode = QStringLiteral("output_missing");
        result.errorMessage = QStringLiteral("RAW 预览输出路径为空");
        return result;
    }

    const auto cacheIdentity = RawPreviewCacheKey::fromSource(
        sourcePath, baseCachePath, maxEdge);
    result.sourceSize = cacheIdentity.sourceSize;
    result.sourceModifiedMs = cacheIdentity.sourceModifiedMs;
    result.decoderPackageVersion = cacheIdentity.decoderPackageVersion;
    result.profileVersion = cacheIdentity.profileVersion;
    result.generatorProfile = cacheIdentity.generatorProfile;
    result.cacheKey = cacheIdentity.cacheKey;
    const auto outputPath = cacheIdentity.outputPath;
    const auto cachedPreview = validatedCachedPreview(outputPath, maxEdge);
    if (!cachedPreview.isNull()) {
        result.success = true;
        result.outputPath = QFileInfo(outputPath).absoluteFilePath();
        result.provider = QStringLiteral("cache");
        result.width = cachedPreview.width();
        result.height = cachedPreview.height();
        return result;
    }

    const auto tryProvider = [&](const QString &providerName,
                                 const std::function<ProviderImage(QString *)> &provider,
                                 bool placeholder) -> bool {
        QString errorMessage;
        auto image = normalizedPreview(provider(&errorMessage), maxEdge);
        if (image.isNull()) {
            addAttempt(&result.attempts, providerName, errorMessage);
            return false;
        }
        if (!writeValidatedJpeg(image, outputPath, &errorMessage)) {
            result.errorCode = QStringLiteral("cache_write_failed");
            result.errorMessage = errorMessage;
            addAttempt(&result.attempts, providerName, errorMessage);
            return true;
        }
        result.success = true;
        result.outputPath = QFileInfo(outputPath).absoluteFilePath();
        result.provider = providerName;
        result.placeholder = placeholder;
        result.width = image.width();
        result.height = image.height();
        return true;
    };

    auto providerIndex = 0;
    if (isGpr) {
        if (providerStartIndex <= providerIndex
            && tryProvider(QStringLiteral("gopro_gpr_sdk"),
                           [&](QString *error) { return decodeGprSdk(sourcePath, error); },
                           false)) {
            return result;
        }
        ++providerIndex;
    }
    if (providerStartIndex <= providerIndex
        && tryProvider(QStringLiteral("libraw_embedded"),
                    [&](QString *error) { return decodeLibRawEmbedded(sourcePath, error); },
                    false)) {
        return result;
    }
    ++providerIndex;
    if (providerStartIndex <= providerIndex
        && tryProvider(QStringLiteral("libraw_rendered"),
                    [&](QString *error) { return decodeLibRawRendered(sourcePath, error); },
                    false)) {
        return result;
    }
    ++providerIndex;
    if (providerStartIndex <= providerIndex
        && tryProvider(QStringLiteral("exiftool_embedded"),
                    [&](QString *error) { return decodeExifToolPreview(sourcePath, error); },
                    false)) {
        return result;
    }
    ++providerIndex;
    if (providerStartIndex <= providerIndex
        && tryProvider(QStringLiteral("wic"),
                    [&](QString *error) { return decodeWic(sourcePath, maxEdge, error); },
                    false)) {
        return result;
    }
    ++providerIndex;
    if (providerStartIndex <= providerIndex
        && tryProvider(QStringLiteral("ffmpeg"),
                    [&](QString *error) { return decodeFfmpeg(sourcePath, maxEdge, error); },
                    false)) {
        return result;
    }
    tryProvider(QStringLiteral("placeholder"),
                [&](QString *) { return rawPlaceholder(sourcePath, maxEdge); },
                true);
    if (!result.success && result.errorCode.isEmpty()) {
        result.errorCode = QStringLiteral("all_providers_failed");
        result.errorMessage = QStringLiteral("所有 RAW 预览 provider 均失败");
    }
    return result;
}
