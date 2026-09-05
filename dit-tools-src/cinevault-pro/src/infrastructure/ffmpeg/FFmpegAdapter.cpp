#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include "shared/VisualAnalysisMetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QImage>
#include <QRegularExpression>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <numeric>
#include <utility>

namespace {
struct ProcessResult {
    bool ok = false;
    bool retryable = false;
    int exitCode = -1;
    QByteArray output;
    QByteArray errorOutput;
    QString errorMessage;
};

struct FrameTimelineProbeResult {
    bool ok = false;
    int frameCount = 0;
    qint64 firstTimestampMs = 0;
    qint64 terminalTimestampMs = 0;
    QString errorMessage;
};

struct TimestampProcessResult {
    ProcessResult process;
    QVector<qint64> timestamps;
};

constexpr qsizetype MaximumProcessErrorBytes = 64 * 1024;

void appendBoundedError(QByteArray *target, const QByteArray &chunk)
{
    if (!target || chunk.isEmpty()) {
        return;
    }
    target->append(chunk);
    if (target->size() > MaximumProcessErrorBytes) {
        target->remove(0, target->size() - MaximumProcessErrorBytes);
    }
}

void finishProcessResult(QProcess &process, ProcessResult *result)
{
    if (!result) {
        return;
    }
    result->exitCode = process.exitCode();
    result->ok = process.exitStatus() == QProcess::NormalExit && result->exitCode == 0;
    if (result->ok) {
        return;
    }
    const auto stderrText = QString::fromUtf8(result->errorOutput).trimmed();
    result->errorMessage = stderrText.isEmpty()
        ? QStringLiteral("命令退出码：%1").arg(result->exitCode)
        : stderrText;
    const auto normalizedError = result->errorMessage.toCaseFolded();
    result->retryable = normalizedError.contains(QStringLiteral("temporarily unavailable"))
        || normalizedError.contains(QStringLiteral("input/output error"))
        || normalizedError.contains(QStringLiteral("i/o error"))
        || normalizedError.contains(QStringLiteral("no such file"))
        || normalizedError.contains(QStringLiteral("sharing violation"));
}

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

    result.output = process.readAllStandardOutput();
    result.errorOutput = process.readAllStandardError();
    finishProcessResult(process, &result);
    return result;
}

FrameTimelineProbeResult probeFrameTimeline(const QString &program,
                                            const QString &sourcePath,
                                            int timeoutMs)
{
    FrameTimelineProbeResult result;
    QProcess process;
    process.setProgram(program);
    process.setArguments({
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"), QStringLiteral("frame=best_effort_timestamp_time"),
        QStringLiteral("-of"), QStringLiteral("csv=p=0"),
        sourcePath
    });
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        result.errorMessage = QStringLiteral("无法启动 ffprobe：%1").arg(process.errorString());
        return result;
    }

    QByteArray pendingOutput;
    QByteArray errorTail;
    QElapsedTimer elapsed;
    elapsed.start();
    const auto consumeLines = [&](bool includeTrailingLine) {
        while (true) {
            const auto newline = pendingOutput.indexOf('\n');
            if (newline < 0 && !includeTrailingLine) {
                break;
            }
            const auto line = newline < 0 ? std::exchange(pendingOutput, QByteArray{})
                                          : pendingOutput.left(newline);
            if (newline >= 0) {
                pendingOutput.remove(0, newline + 1);
            }
            const auto value = line.trimmed().split(',').value(0).trimmed();
            if (value.isEmpty()) {
                if (newline < 0) break;
                continue;
            }
            ++result.frameCount;
            bool ok = false;
            const auto timestampSeconds = value.toDouble(&ok);
            if (ok) {
                const auto timestampMs = qRound64(timestampSeconds * 1000.0);
                if (result.frameCount == 1) {
                    result.firstTimestampMs = timestampMs;
                }
                result.terminalTimestampMs = timestampMs;
            }
            if (newline < 0) break;
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        pendingOutput.append(process.readAllStandardOutput());
        appendBoundedError(&errorTail, process.readAllStandardError());
        consumeLines(false);
        if (elapsed.elapsed() > timeoutMs) {
            process.kill();
            process.waitForFinished(3000);
            result.errorMessage = QStringLiteral("ffprobe 读取视频时间轴超时");
            return result;
        }
    }
    pendingOutput.append(process.readAllStandardOutput());
    appendBoundedError(&errorTail, process.readAllStandardError());
    consumeLines(true);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.errorMessage = QString::fromUtf8(errorTail).trimmed();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("ffprobe 退出码：%1").arg(process.exitCode());
        }
        return result;
    }
    if (result.frameCount <= 0) {
        result.errorMessage = QStringLiteral("视频中没有可抽取的帧");
        return result;
    }
    result.ok = true;
    return result;
}

TimestampProcessResult runTimestampProcess(const QString &program,
                                           const QStringList &arguments,
                                           int timeoutMs)
{
    TimestampProcessResult result;
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        result.process.retryable = true;
        result.process.errorMessage = QStringLiteral("无法启动命令：%1").arg(process.errorString());
        return result;
    }

    static const QRegularExpression timestampExpression(QStringLiteral(
        "pts_time:([-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?)"));
    QByteArray pendingError;
    QElapsedTimer elapsed;
    elapsed.start();
    const auto consumeLines = [&](bool includeTrailingLine) {
        while (true) {
            const auto newline = pendingError.indexOf('\n');
            if (newline < 0 && !includeTrailingLine) {
                break;
            }
            const auto line = newline < 0 ? std::exchange(pendingError, QByteArray{})
                                          : pendingError.left(newline);
            if (newline >= 0) {
                pendingError.remove(0, newline + 1);
            }
            const auto text = QString::fromUtf8(line);
            const auto match = timestampExpression.match(text);
            if (match.hasMatch()) {
                bool ok = false;
                const auto seconds = match.captured(1).toDouble(&ok);
                if (ok) {
                    result.timestamps.append(qRound64(seconds * 1000.0));
                }
            }
            appendBoundedError(&result.process.errorOutput, line + '\n');
            if (newline < 0) break;
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        pendingError.append(process.readAllStandardError());
        process.readAllStandardOutput();
        consumeLines(false);
        if (elapsed.elapsed() > timeoutMs) {
            process.kill();
            process.waitForFinished(3000);
            result.process.retryable = true;
            result.process.errorMessage = QStringLiteral("命令执行超时");
            return result;
        }
    }
    pendingError.append(process.readAllStandardError());
    consumeLines(true);
    finishProcessResult(process, &result.process);
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

QString storyboardFilter(VideoFrameExtractionStrategy strategy,
                         double intervalSeconds,
                         double sceneThreshold)
{
    const auto seconds = qBound(0.1, intervalSeconds, 240.0);
    const auto threshold = qBound(0.05, sceneThreshold, 0.95);
    switch (strategy) {
    case VideoFrameExtractionStrategy::PerFrame:
        return QStringLiteral("null");
    case VideoFrameExtractionStrategy::SceneAndInterval:
        return QStringLiteral("select=eq(n\\,0)+gt(scene\\,%1)+gte(t-prev_selected_t\\,%2)")
            .arg(threshold, 0, 'f', 2)
            .arg(seconds, 0, 'f', 3);
    case VideoFrameExtractionStrategy::IntervalOnly:
    case VideoFrameExtractionStrategy::HighFidelity:
        return QStringLiteral("fps=1/%1").arg(seconds, 0, 'f', 3);
    }
    return QStringLiteral("fps=1/%1").arg(seconds, 0, 'f', 3);
}

struct CandidateFrame {
    QString imagePath;
    qint64 timestampMs = 0;
    bool requiredAnchor = false;
};

struct AcceptedFingerprint {
    quint64 hash = 0;
    qint64 timestampMs = 0;
};

double luminance(QRgb pixel)
{
    return 0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) + 0.0722 * qBlue(pixel);
}

struct FrameQuality {
    bool valid = false;
    double sharpness = 0.0;
    double brightness = 0.0;
    quint64 hash = 0;
};

FrameQuality inspectFrameQuality(const QString &path)
{
    const QImage image(path);
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return {};
    }

    const auto step = qMax(1, qCeil(qSqrt((image.width() * image.height()) / 250000.0)));
    double luminanceTotal = 0.0;
    double gradientTotal = 0.0;
    int count = 0;
    for (int y = 0; y < image.height(); y += step) {
        for (int x = 0; x < image.width(); x += step) {
            const auto value = luminance(image.pixel(x, y));
            luminanceTotal += value;
            if (x + step < image.width()) {
                gradientTotal += qAbs(value - luminance(image.pixel(x + step, y)));
            }
            if (y + step < image.height()) {
                gradientTotal += qAbs(value - luminance(image.pixel(x, y + step)));
            }
            ++count;
        }
    }

    QVector<double> hashValues;
    hashValues.reserve(64);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const auto sourceX = qBound(0, qFloor((x + 0.5) * image.width() / 8.0), image.width() - 1);
            const auto sourceY = qBound(0, qFloor((y + 0.5) * image.height() / 8.0), image.height() - 1);
            hashValues.append(luminance(image.pixel(sourceX, sourceY)));
        }
    }
    const auto average = std::accumulate(hashValues.cbegin(), hashValues.cend(), 0.0) / hashValues.size();
    quint64 hash = 0;
    for (const auto value : hashValues) {
        hash = (hash << 1) | (value >= average ? 1 : 0);
    }
    return {true,
            count == 0 ? 0.0 : qBound(0.0, gradientTotal / (count * 2 * 255.0), 1.0),
            count == 0 ? 0.0 : qBound(0.0, luminanceTotal / (count * 255.0), 1.0),
            hash};
}

int hashDistance(quint64 left, quint64 right)
{
    auto value = left ^ right;
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
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

    const auto timeline = probeFrameTimeline(m_ffprobePath, request.sourcePath, 120000);
    if (!timeline.ok) {
        result.errorMessage = QStringLiteral("ffprobe 读取视频时间轴失败：%1")
                                  .arg(timeline.errorMessage);
        return result;
    }

    result.sourceFrameCount = timeline.frameCount;
    const auto firstSourceTimestampMs = timeline.firstTimestampMs;
    const auto terminalSourceTimestampMs = timeline.terminalTimestampMs;
    const auto effectiveInterval = request.strategy == VideoFrameExtractionStrategy::HighFidelity
        ? qBound(0.1, request.intervalSeconds, 0.25)
        : qBound(0.1, request.intervalSeconds, 240.0);
    result.frameInterval = qRound(effectiveInterval * 1000.0);
    const auto candidatePattern = QDir(request.outputDirectory).filePath(QStringLiteral("candidate_%06d.jpg"));
    const auto extractionFilter = QStringLiteral("%1,%2,format=yuvj420p,showinfo")
                                      .arg(storyboardFilter(request.strategy,
                                                            effectiveInterval,
                                                            request.sceneThreshold),
                                           scaleFilter(request.maxWidth, request.maxHeight));
    QStringList extractionArguments = {
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("info"),
        QStringLiteral("-i"), request.sourcePath,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-vf"), extractionFilter,
        QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
        QStringLiteral("-q:v"), QStringLiteral("2"),
        QStringLiteral("-start_number"), QStringLiteral("0"),
        candidatePattern
    };
    const auto extraction = runTimestampProcess(m_ffmpegPath, extractionArguments, 120000);
    const auto &extractionProcess = extraction.process;
    const auto candidateFiles = QDir(request.outputDirectory).entryInfoList(
        {QStringLiteral("candidate_*.jpg")}, QDir::Files, QDir::Name);
    if (!extractionProcess.ok && !candidateFiles.isEmpty()) {
        result.errorMessage = QStringLiteral("按 filmstoryboard 规则抽帧失败：%1")
                                  .arg(extractionProcess.errorMessage);
        return result;
    }
    const auto &timestamps = extraction.timestamps;
    QVector<CandidateFrame> candidates;
    candidates.reserve(candidateFiles.size() + 1);
    for (int index = 0; index < candidateFiles.size(); ++index) {
        qint64 timestampMs = 0;
        if (index < timestamps.size()) {
            timestampMs = timestamps.at(index);
        } else if (request.strategy == VideoFrameExtractionStrategy::PerFrame
                   && result.sourceFrameCount > 1) {
            const auto sourceSpanMs = terminalSourceTimestampMs - firstSourceTimestampMs;
            timestampMs = firstSourceTimestampMs
                + qRound64(index * sourceSpanMs / static_cast<double>(result.sourceFrameCount - 1));
        } else {
            timestampMs = firstSourceTimestampMs
                + qRound64(index * effectiveInterval * 1000.0);
        }
        candidates.append({candidateFiles.at(index).absoluteFilePath(), timestampMs, index == 0});
    }

    const auto terminalToleranceMs = qMax<qint64>(1, qMin<qint64>(250, result.frameInterval / 4));
    auto terminalCandidate = std::min_element(
        candidates.begin(), candidates.end(), [terminalSourceTimestampMs](const auto &left, const auto &right) {
            return qAbs(left.timestampMs - terminalSourceTimestampMs)
                < qAbs(right.timestampMs - terminalSourceTimestampMs);
        });
    const auto terminalAlreadyCovered = terminalCandidate != candidates.end()
        && qAbs(terminalCandidate->timestampMs - terminalSourceTimestampMs) <= terminalToleranceMs;
    if (terminalAlreadyCovered) {
        terminalCandidate->requiredAnchor = true;
        terminalCandidate->timestampMs = terminalSourceTimestampMs;
    }
    if (!terminalAlreadyCovered) {
        const auto terminalPath = outputDir.filePath(QStringLiteral("candidate_terminal.jpg"));
        const auto tailWindowMs = qMin<qint64>(5000, qMax<qint64>(1000, result.frameInterval * 2LL));
        const auto seekStartMs = qMax(firstSourceTimestampMs,
                                      terminalSourceTimestampMs - tailWindowMs);
        const auto relativeSeekMs = qMax<qint64>(0, terminalSourceTimestampMs - seekStartMs);
        const QStringList terminalArguments = {
            QStringLiteral("-y"),
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QString::number(seekStartMs / 1000.0, 'f', 3),
            QStringLiteral("-i"), request.sourcePath,
            QStringLiteral("-ss"), QString::number(relativeSeekMs / 1000.0, 'f', 3),
            QStringLiteral("-map"), QStringLiteral("0:v:0"),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("%1,format=yuvj420p")
                                      .arg(scaleFilter(request.maxWidth, request.maxHeight)),
            QStringLiteral("-q:v"), QStringLiteral("2"),
            terminalPath
        };
        const auto terminalProcess = runProcess(m_ffmpegPath, terminalArguments, 120000);
        const QFileInfo terminalFile(terminalPath);
        if (!terminalProcess.ok || !terminalFile.isFile() || terminalFile.size() <= 0) {
            result.errorMessage = QStringLiteral("无法抽取视频尾部锚点：%1")
                                      .arg(terminalProcess.errorMessage);
            return result;
        }
        candidates.append({terminalPath, terminalSourceTimestampMs, true});
    }
    if (candidates.isEmpty()) {
        result.errorMessage = extractionProcess.errorMessage.isEmpty()
            ? QStringLiteral("当前抽帧策略没有生成候选帧")
            : QStringLiteral("按 filmstoryboard 规则抽帧失败：%1")
                  .arg(extractionProcess.errorMessage);
        return result;
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        if (left.timestampMs != right.timestampMs) {
            return left.timestampMs < right.timestampMs;
        }
        return left.imagePath < right.imagePath;
    });

    constexpr qint64 DuplicateWindowMs = 1000;
    QVector<AcceptedFingerprint> recentFingerprints;
    QVector<ExtractedFrame> selectedFrames;
    selectedFrames.reserve(candidates.size());
    for (int index = 0; index < candidates.size(); ++index) {
        const auto &candidate = candidates.at(index);
        const auto quality = inspectFrameQuality(candidate.imagePath);
        recentFingerprints.erase(
            std::remove_if(recentFingerprints.begin(),
                           recentFingerprints.end(),
                           [&candidate](const auto &item) {
                               return candidate.timestampMs - item.timestampMs >= DuplicateWindowMs;
                           }),
            recentFingerprints.end());
        auto duplicate = false;
        for (const auto &item : recentFingerprints) {
            if (hashDistance(item.hash, quality.hash) <= 4) {
                duplicate = true;
                break;
            }
        }
        const auto passesQuality = quality.sharpness >= qBound(0.0, request.minimumSharpness, 1.0)
            && quality.brightness >= 0.08
            && quality.brightness <= 0.92;
        const auto valid = quality.valid
            && (candidate.requiredAnchor || (passesQuality && !duplicate));
        if (!valid) {
            QFile::remove(candidate.imagePath);
            continue;
        }
        recentFingerprints.append({quality.hash, candidate.timestampMs});
        ExtractedFrame frame;
        frame.frameNumber = selectedFrames.size() + 1;
        frame.timestampMs = candidate.timestampMs;
        frame.imagePath = QDir(request.outputDirectory).filePath(
            QStringLiteral("frame_%1.jpg").arg(frame.frameNumber, 6, 10, QLatin1Char('0')));
        QFile::remove(frame.imagePath);
        if (!QFile::rename(candidate.imagePath, frame.imagePath)) {
            result.errorMessage = QStringLiteral("无法保存筛选后的候选帧：%1").arg(frame.imagePath);
            return result;
        }
        selectedFrames.append(frame);
    }
    if (selectedFrames.isEmpty()) {
        result.errorMessage = QStringLiteral("候选帧均因清晰度、曝光或重复画面被筛除");
        return result;
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
