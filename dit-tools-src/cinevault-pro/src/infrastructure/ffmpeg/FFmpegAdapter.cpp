#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include "shared/VisualAnalysisMetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSet>
#include <QStringList>

namespace {
struct ProcessResult {
    bool ok = false;
    bool retryable = false;
    int exitCode = -1;
    QByteArray output;
    QByteArray errorOutput;
    QString errorMessage;
};

QString envPath(const char *name)
{
    return QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv(name)).trimmed());
}

QString appPath()
{
    return QDir::fromNativeSeparators(QCoreApplication::applicationDirPath().trimmed());
}

QString existingFile(const QStringList &candidates)
{
    for (const auto &candidate : candidates) {
        if (candidate.isEmpty()) {
            continue;
        }
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString exeFromBin(const QString &binRoot, const QString &exeName)
{
    if (binRoot.isEmpty()) {
        return {};
    }
    return QDir(binRoot).filePath(exeName);
}

QString exeFromRoot(const QString &root, const QString &exeName)
{
    if (root.isEmpty()) {
        return {};
    }
    return QDir(root).filePath(QStringLiteral("bin/%1").arg(exeName));
}

ProcessResult runProcess(const QString &program, const QStringList &arguments, int timeoutMs)
{
    ProcessResult result;
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted(5000)) {
        result.retryable = true;
        result.errorMessage = QStringLiteral("无法启动命令：%1").arg(process.errorString());
        return result;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        result.retryable = true;
        result.errorMessage = QStringLiteral("命令执行超时");
        result.output = process.readAllStandardOutput();
        result.errorOutput = process.readAllStandardError();
        return result;
    }

    result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    result.errorOutput = process.readAllStandardError();
    result.ok = process.exitStatus() == QProcess::NormalExit && result.exitCode == 0;
    if (!result.ok) {
        const auto stderrText = QString::fromUtf8(result.errorOutput).trimmed();
        result.errorMessage = stderrText.isEmpty()
            ? QStringLiteral("命令退出码：%1").arg(result.exitCode)
            : stderrText;
        const auto normalizedError = result.errorMessage.toCaseFolded();
        result.retryable = normalizedError.contains(QStringLiteral("temporarily unavailable"))
            || normalizedError.contains(QStringLiteral("input/output error"))
            || normalizedError.contains(QStringLiteral("i/o error"))
            || normalizedError.contains(QStringLiteral("no such file"))
            || normalizedError.contains(QStringLiteral("sharing violation"));
    }
    return result;
}

qint64 jsonLong(const QJsonValue &value)
{
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    bool ok = false;
    const auto parsed = value.toString().toDouble(&ok);
    return ok ? static_cast<qint64>(parsed) : 0;
}

int jsonInt(const QJsonValue &value)
{
    return static_cast<int>(jsonLong(value));
}

QString scaleFilter(int maxWidth, int maxHeight)
{
    return QStringLiteral("scale='min(%1,iw)':'min(%2,ih)':force_original_aspect_ratio=decrease")
        .arg(qMax(1, maxWidth))
        .arg(qMax(1, maxHeight));
}
}

FFmpegAdapter::FFmpegAdapter()
{
    const auto ffmpegBinRoot = envPath("CINEVAULT_FFMPEG_BIN");
    const auto ffmpegRoot = envPath("CINEVAULT_FFMPEG_ROOT");
    const auto legacyDevRoot = envPath("FFMPEG_DEV_ROOT");
    const auto defaultRoot = QStringLiteral("G:/data/app/DIT/ffmpeg");
    const auto bundledAppDir = appPath();
    const auto bundledFfmpegRoot = bundledAppDir.isEmpty()
        ? QString()
        : QDir(bundledAppDir).filePath(QStringLiteral("ffmpeg"));

    m_ffprobePath = existingFile({
        envPath("CINEVAULT_FFPROBE_PATH"),
        exeFromBin(ffmpegBinRoot, QStringLiteral("ffprobe.exe")),
        exeFromRoot(ffmpegRoot, QStringLiteral("ffprobe.exe")),
        exeFromRoot(bundledFfmpegRoot, QStringLiteral("ffprobe.exe")),
        exeFromBin(bundledAppDir, QStringLiteral("ffprobe.exe")),
        exeFromRoot(legacyDevRoot, QStringLiteral("ffprobe.exe")),
        exeFromRoot(defaultRoot, QStringLiteral("ffprobe.exe"))
    });
    m_ffmpegPath = existingFile({
        envPath("CINEVAULT_FFMPEG_PATH"),
        exeFromBin(ffmpegBinRoot, QStringLiteral("ffmpeg.exe")),
        exeFromRoot(ffmpegRoot, QStringLiteral("ffmpeg.exe")),
        exeFromRoot(bundledFfmpegRoot, QStringLiteral("ffmpeg.exe")),
        exeFromBin(bundledAppDir, QStringLiteral("ffmpeg.exe")),
        exeFromRoot(legacyDevRoot, QStringLiteral("ffmpeg.exe")),
        exeFromRoot(defaultRoot, QStringLiteral("ffmpeg.exe"))
    });

    m_available = !m_ffprobePath.isEmpty() && !m_ffmpegPath.isEmpty();
    if (!m_available) {
        QStringList missing;
        if (m_ffprobePath.isEmpty()) {
            missing.append(QStringLiteral("ffprobe.exe"));
        }
        if (m_ffmpegPath.isEmpty()) {
            missing.append(QStringLiteral("ffmpeg.exe"));
        }
        m_unavailableReason = QStringLiteral("未找到命令行 FFmpeg：%1").arg(missing.join(QStringLiteral("、")));
    }
}

bool FFmpegAdapter::isAvailable() const
{
    return m_available;
}

QString FFmpegAdapter::unavailableReason() const
{
    return m_unavailableReason;
}

MediaProbeResult FFmpegAdapter::probe(const AssetFile &asset) const
{
    MediaProbeResult result;
    result.assetId = asset.id;
    result.mediaType = asset.assetType == AssetType::Video
        ? MediaType::Video
        : (asset.assetType == AssetType::Audio ? MediaType::Audio : MediaType::Image);

    if (m_ffprobePath.isEmpty()) {
        result.status = ProbeStatus::Unavailable;
        result.errorMessage = m_unavailableReason;
        return result;
    }

    const QStringList arguments = {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"),
        QStringLiteral("-show_streams"),
        QStringLiteral("-show_chapters"),
        QStringLiteral("-show_programs"),
        asset.absolutePath
    };
    const auto process = runProcess(m_ffprobePath, arguments, 60000);
    result.rawJson = QString::fromUtf8(process.output);
    if (!process.ok) {
        result.status = ProbeStatus::Failed;
        result.errorMessage = QStringLiteral("ffprobe 执行失败：%1").arg(process.errorMessage);
        return result;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(process.output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.status = ProbeStatus::Failed;
        result.errorMessage = QStringLiteral("ffprobe 输出解析失败：%1").arg(parseError.errorString());
        return result;
    }

    const auto root = document.object();
    const auto format = root.value(QStringLiteral("format")).toObject();
    result.format.container = format.value(QStringLiteral("format_name")).toString();
    result.format.durationMs = static_cast<qint64>(format.value(QStringLiteral("duration")).toString().toDouble() * 1000.0);
    result.format.bitRate = jsonLong(format.value(QStringLiteral("bit_rate")));

    const auto streams = root.value(QStringLiteral("streams")).toArray();
    for (const auto &streamValue : streams) {
        const auto streamObject = streamValue.toObject();
        StreamInfo stream;
        stream.index = jsonInt(streamObject.value(QStringLiteral("index")));
        stream.codec = streamObject.value(QStringLiteral("codec_name")).toString();
        stream.kind = streamObject.value(QStringLiteral("codec_type")).toString();
        stream.bitRate = jsonLong(streamObject.value(QStringLiteral("bit_rate")));
        stream.width = jsonInt(streamObject.value(QStringLiteral("width")));
        stream.height = jsonInt(streamObject.value(QStringLiteral("height")));
        stream.channels = jsonInt(streamObject.value(QStringLiteral("channels")));
        stream.sampleRate = jsonInt(streamObject.value(QStringLiteral("sample_rate")));
        if (stream.kind == QStringLiteral("video")) {
            result.mediaType = MediaType::Video;
        } else if (stream.kind == QStringLiteral("audio") && result.mediaType != MediaType::Video) {
            result.mediaType = MediaType::Audio;
        }
        result.streams.append(stream);
    }

    if (result.streams.isEmpty()) {
        result.status = ProbeStatus::Unsupported;
        result.errorMessage = QStringLiteral("未识别到可用媒体流");
        return result;
    }

    result.status = ProbeStatus::Success;

    return result;
}

FrameExtractionResult FFmpegAdapter::extractFrames(const FrameExtractionRequest &request) const
{
    FrameExtractionResult result;
    result.assetId = request.assetId;

    if (m_ffprobePath.isEmpty() || m_ffmpegPath.isEmpty()) {
        result.errorMessage = m_unavailableReason;
        return result;
    }
    if (request.sourcePath.isEmpty() || request.outputDirectory.isEmpty()) {
        result.errorMessage = QStringLiteral("抽帧输入或输出目录为空");
        return result;
    }

    QDir outputDir(request.outputDirectory);
    if (outputDir.exists() && !request.preserveExistingFrames && !outputDir.removeRecursively()) {
        result.errorMessage = QStringLiteral("无法清理抽帧目录：%1").arg(outputDir.absolutePath());
        return result;
    }
    if (!QDir().mkpath(request.outputDirectory)) {
        result.errorMessage = QStringLiteral("无法创建抽帧目录：%1").arg(request.outputDirectory);
        return result;
    }

    const QStringList probeArguments = {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"), QStringLiteral("frame=best_effort_timestamp_time"),
        QStringLiteral("-of"), QStringLiteral("json"),
        request.sourcePath
    };
    const auto probeProcess = runProcess(m_ffprobePath, probeArguments, 120000);
    if (!probeProcess.ok) {
        result.errorMessage = QStringLiteral("ffprobe 读取视频帧失败：%1").arg(probeProcess.errorMessage);
        return result;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(probeProcess.output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errorMessage = QStringLiteral("解析视频帧列表失败：%1").arg(parseError.errorString());
        return result;
    }

    const auto frames = document.object().value(QStringLiteral("frames")).toArray();
    if (frames.isEmpty()) {
        result.errorMessage = QStringLiteral("视频中没有可抽取的帧");
        return result;
    }

    result.sourceFrameCount = frames.size();
    result.frameInterval = VisualAnalysisMetadata::fixedFrameInterval(request.mode, request.frameInterval);

    QSet<int> requestedFrameNumbers;
    for (const auto frameNumber : request.requestedFrameNumbers) {
        if (frameNumber > 0) {
            requestedFrameNumbers.insert(frameNumber);
        }
    }

    QVector<ExtractedFrame> selectedFrames;
    selectedFrames.reserve(frames.size());
    const int interval = result.frameInterval;
    for (int index = 0; index < frames.size(); ++index) {
        const auto frameNumber = index + 1;
        if ((index % interval) != 0
            || (!requestedFrameNumbers.isEmpty() && !requestedFrameNumbers.contains(frameNumber))) {
            continue;
        }

        const auto frameObject = frames.at(index).toObject();
        bool ok = false;
        const auto timestampSec = frameObject.value(QStringLiteral("best_effort_timestamp_time")).toString().toDouble(&ok);
        ExtractedFrame frame;
        frame.frameNumber = frameNumber;
        frame.timestampMs = ok ? static_cast<qint64>(timestampSec * 1000.0) : 0;
        frame.imagePath = QDir(request.outputDirectory).filePath(
            QStringLiteral("frame_%1.jpg").arg(frame.frameNumber, 6, 10, QLatin1Char('0')));
        selectedFrames.append(frame);
    }

    if (selectedFrames.isEmpty()) {
        result.errorMessage = QStringLiteral("没有匹配当前抽帧设置的帧");
        return result;
    }

    const auto filter = scaleFilter(request.maxWidth, request.maxHeight);
    for (auto &frame : selectedFrames) {
        if (request.preserveExistingFrames) {
            const QFileInfo existing(frame.imagePath);
            if (existing.isFile() && existing.size() > 0) {
                continue;
            }
        }
        QFile::remove(frame.imagePath);
        const auto secondsText = QString::number(qMax<qint64>(0, frame.timestampMs) / 1000.0, 'f', 3);
        const QStringList arguments = {
            QStringLiteral("-y"),
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), secondsText,
            QStringLiteral("-i"), request.sourcePath,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), filter,
            QStringLiteral("-q:v"), QStringLiteral("3"),
            frame.imagePath
        };
        const auto process = runProcess(m_ffmpegPath, arguments, 120000);
        if (!process.ok) {
            result.errorMessage = QStringLiteral("抽取视频帧失败：第 %1 帧，%2").arg(frame.frameNumber).arg(process.errorMessage);
            return result;
        }

        const QFileInfo info(frame.imagePath);
        if (!info.exists() || info.size() <= 0) {
            result.errorMessage = QStringLiteral("抽取视频帧失败：第 %1 帧未生成有效图片").arg(frame.frameNumber);
            return result;
        }
    }

    result.success = true;
    result.frames = selectedFrames;
    return result;
}

ThumbnailResult FFmpegAdapter::generateThumbnail(const ThumbnailRequest &request) const
{
    ThumbnailResult result;
    result.assetId = request.assetId;

    if (m_ffmpegPath.isEmpty()) {
        result.errorMessage = m_unavailableReason;
        return result;
    }
    if (request.sourcePath.isEmpty() || request.cachePath.isEmpty()) {
        result.errorMessage = QStringLiteral("缩略图输入或缓存路径为空");
        return result;
    }

    const QFileInfo outputInfo(request.cachePath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        result.errorMessage = QStringLiteral("无法创建缩略图缓存目录：%1").arg(outputInfo.absolutePath());
        return result;
    }
    if (outputInfo.exists()) {
        QFile::remove(outputInfo.absoluteFilePath());
    }

    const auto zeroBasedFrame = qMax(0, request.frameIndex - 1);
    const QStringList arguments = {
        QStringLiteral("-y"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), request.sourcePath,
        QStringLiteral("-vf"), QStringLiteral("select=eq(n\\,%1),%2").arg(zeroBasedFrame).arg(scaleFilter(request.maxWidth, request.maxHeight)),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vsync"), QStringLiteral("vfr"),
        QStringLiteral("-q:v"), QStringLiteral("3"),
        request.cachePath
    };
    const auto process = runProcess(m_ffmpegPath, arguments, 60000);
    if (!process.ok) {
        result.retryable = process.retryable;
        result.errorMessage = QStringLiteral("ffmpeg 生成缩略图失败：%1").arg(process.errorMessage);
        return result;
    }

    const QFileInfo generated(request.cachePath);
    if (!generated.exists() || generated.size() <= 0) {
        result.errorMessage = QStringLiteral("ffmpeg 未生成有效缩略图文件");
        return result;
    }

    result.success = true;
    result.outputPath = generated.absoluteFilePath();

    return result;
}
