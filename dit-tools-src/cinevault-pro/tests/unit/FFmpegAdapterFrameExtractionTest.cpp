#include "infrastructure/ffmpeg/FFmpegAdapter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {
QString existingExecutable(const QString &environmentName,
                           const QString &executableName,
                           const QStringList &fallbacks)
{
    const auto environmentPath = QDir::fromNativeSeparators(
        QString::fromLocal8Bit(qgetenv(environmentName.toLocal8Bit().constData())).trimmed());
    if (!environmentPath.isEmpty() && QFileInfo(environmentPath).isFile()) {
        return QFileInfo(environmentPath).absoluteFilePath();
    }
    const auto pathExecutable = QStandardPaths::findExecutable(executableName);
    if (!pathExecutable.isEmpty()) {
        return QFileInfo(pathExecutable).absoluteFilePath();
    }
    for (const auto &fallback : fallbacks) {
        const auto candidate = QDir::fromNativeSeparators(fallback);
        if (QFileInfo(candidate).isFile()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

bool runProcess(const QString &program, const QStringList &arguments, QString *errorMessage)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        if (errorMessage) {
            *errorMessage = process.errorString();
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(process.readAllStandardError()).trimmed();
        }
        return false;
    }
    return true;
}
}

class FFmpegAdapterFrameExtractionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_ffmpegPath = existingExecutable(
            QStringLiteral("CINEVAULT_FFMPEG_PATH"),
            QStringLiteral("ffmpeg.exe"),
            {
                QStringLiteral("C:/Program Files/影资管家/ffmpeg/bin/ffmpeg.exe"),
                QStringLiteral("G:/data/app/DIT/ffmpeg/bin/ffmpeg.exe")
            });
        m_ffprobePath = existingExecutable(
            QStringLiteral("CINEVAULT_FFPROBE_PATH"),
            QStringLiteral("ffprobe.exe"),
            {
                QStringLiteral("C:/Program Files/影资管家/ffmpeg/bin/ffprobe.exe"),
                QStringLiteral("G:/data/app/DIT/ffmpeg/bin/ffprobe.exe")
            });
        if (m_ffmpegPath.isEmpty() || m_ffprobePath.isEmpty()) {
            QSKIP("FFmpeg CLI runtime is unavailable");
        }

        qputenv("CINEVAULT_FFMPEG_PATH", m_ffmpegPath.toLocal8Bit());
        qputenv("CINEVAULT_FFPROBE_PATH", m_ffprobePath.toLocal8Bit());
    }

    void intervalSampling_scalesAndCoversTerminalFrame()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto sourcePath = createVideo(
            temporaryDir,
            QStringLiteral("interval.mkv"),
            QStringLiteral("color=c=gray:size=256x128:rate=10,drawgrid=w=16:h=128:t=1:c=black"),
            73);

        FFmpegAdapter adapter;
        QVERIFY2(adapter.isAvailable(), qPrintable(adapter.unavailableReason()));
        FrameExtractionRequest request;
        request.sourcePath = sourcePath;
        request.outputDirectory = QDir(temporaryDir.path()).filePath(QStringLiteral("frames"));
        request.strategy = VideoFrameExtractionStrategy::SceneAndInterval;
        request.intervalSeconds = 2.0;
        request.maxWidth = 64;
        request.maxHeight = 64;

        const auto result = adapter.extractFrames(request);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.sourceFrameCount, 73);
        QVERIFY(result.frames.size() >= 5);
        QCOMPARE(result.frames.first().timestampMs, qint64{0});
        QCOMPARE(result.frames.last().timestampMs, qint64{7200});
        for (int index = 0; index < result.frames.size(); ++index) {
            const auto &frame = result.frames.at(index);
            QCOMPARE(frame.frameNumber, index + 1);
            QVERIFY(index == 0 || frame.timestampMs > result.frames.at(index - 1).timestampMs);
            const QImage image(frame.imagePath);
            QVERIFY2(!image.isNull(), qPrintable(frame.imagePath));
            QVERIFY(image.width() <= request.maxWidth);
            QVERIFY(image.height() <= request.maxHeight);
        }
    }

    void perFrameSampling_keepsReturningSceneAndTerminalTime()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto sourcePath = createVideo(
            temporaryDir,
            QStringLiteral("returning.mkv"),
            QStringLiteral("nullsrc=size=256x128:rate=10,geq=lum='if(between(N,10,19),if(lt(Y,H/2),64,192),if(lt(X,W/2),64,192))':cb=128:cr=128"),
            30);

        FFmpegAdapter adapter;
        FrameExtractionRequest request;
        request.sourcePath = sourcePath;
        request.outputDirectory = QDir(temporaryDir.path()).filePath(QStringLiteral("frames"));
        request.strategy = VideoFrameExtractionStrategy::PerFrame;
        request.minimumSharpness = 0.0;
        request.maxWidth = 64;
        request.maxHeight = 64;

        const auto result = adapter.extractFrames(request);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(std::any_of(result.frames.cbegin(), result.frames.cend(), [](const auto &frame) {
            return frame.timestampMs >= 2000;
        }));
        QCOMPARE(result.frames.last().timestampMs, qint64{2900});
    }

    void shortVideo_isCoveredWhenIntervalExceedsDuration()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto sourcePath = createVideo(
            temporaryDir,
            QStringLiteral("short.mkv"),
            QStringLiteral("color=c=gray:size=64x64:rate=10,drawgrid=w=16:h=64:t=1:c=black"),
            1);

        FFmpegAdapter adapter;
        FrameExtractionRequest request;
        request.sourcePath = sourcePath;
        request.outputDirectory = QDir(temporaryDir.path()).filePath(QStringLiteral("frames"));
        request.strategy = VideoFrameExtractionStrategy::IntervalOnly;
        request.intervalSeconds = 2.0;
        request.maxWidth = 64;
        request.maxHeight = 64;

        const auto result = adapter.extractFrames(request);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.sourceFrameCount, 1);
        QCOMPARE(result.frames.size(), 1);
        QCOMPARE(result.frames.first().timestampMs, qint64{0});
    }

    void variableRateAndNonzeroStartKeepSourceTimestamps_data()
    {
        QTest::addColumn<int>("strategy");
        QTest::newRow("per-frame") << static_cast<int>(VideoFrameExtractionStrategy::PerFrame);
        QTest::newRow("interval") << static_cast<int>(VideoFrameExtractionStrategy::IntervalOnly);
        QTest::newRow("scene") << static_cast<int>(VideoFrameExtractionStrategy::SceneAndInterval);
    }

    void variableRateAndNonzeroStartKeepSourceTimestamps()
    {
        QFETCH(int, strategy);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("vfr-offset.mkv"));
        QString error;
        QVERIFY2(runProcess(m_ffmpegPath,
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-copyts"), QStringLiteral("-f"), QStringLiteral("lavfi"),
             QStringLiteral("-i"), QStringLiteral("testsrc=size=128x64:rate=10"),
             QStringLiteral("-vf"), QStringLiteral("setpts=5/TB+if(lt(N\\,5)\\,N\\,5+(N-5)*3)/(10*TB)"),
             QStringLiteral("-frames:v"), QStringLiteral("10"),
             QStringLiteral("-fps_mode"), QStringLiteral("passthrough"),
             QStringLiteral("-c:v"), QStringLiteral("ffv1"), sourcePath}, &error), qPrintable(error));
        FFmpegAdapter adapter;
        FrameExtractionRequest request;
        request.sourcePath = sourcePath;
        request.outputDirectory = QDir(temp.path()).filePath(QStringLiteral("frames"));
        request.strategy = static_cast<VideoFrameExtractionStrategy>(strategy);
        request.intervalSeconds = 2.0;
        request.maxWidth = 64;
        request.maxHeight = 64;
        const auto result = adapter.extractFrames(request);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.sourceFrameCount, 10);
        QCOMPARE(result.frames.first().timestampMs, qint64{5000});
        QCOMPARE(result.frames.last().timestampMs, qint64{6700});
        if (request.strategy == VideoFrameExtractionStrategy::PerFrame) QCOMPARE(result.frames.size(), 10);
        const QSet<qint64> sourceTimes{5000, 5100, 5200, 5300, 5400, 5500, 5800, 6100, 6400, 6700};
        for (const auto &frame : result.frames) QVERIFY(sourceTimes.contains(frame.timestampMs));
    }

    void twoHourVideoHasBoundedCoverageManifest()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto source = createVideo(temp, QStringLiteral("two-hours.mkv"),
                                         QStringLiteral("color=c=gray:size=64x32:rate=1"), 7201);
        FrameExtractionRequest request;
        request.sourcePath = source;
        request.outputDirectory = QDir(temp.path()).filePath(QStringLiteral("frames"));
        request.strategy = VideoFrameExtractionStrategy::IntervalOnly;
        request.intervalSeconds = 240;
        request.maxWidth = 64;
        request.maxHeight = 64;
        FFmpegAdapter adapter;
        const auto result = adapter.extractFrames(request);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.frames.size(), 31);
        QCOMPARE(result.frames.last().timestampMs, qint64{7200000});
        QFile manifest(QDir(request.outputDirectory).filePath(QStringLiteral("coverage.json")));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(manifest.readAll()).object();
        QCOMPARE(json.value(QStringLiteral("source_frame_count")).toInt(), 7201);
        QCOMPARE(json.value(QStringLiteral("candidates")).toArray().size(), 31);
        QCOMPARE(json.value(QStringLiteral("last_source_pts_ms")).toInteger(), qint64{7200000});
    }

    void realFashionVideoCoversCompleteTimeline_data()
    {
        QTest::addColumn<int>("strategy");
        QTest::newRow("per-frame") << static_cast<int>(VideoFrameExtractionStrategy::PerFrame);
        QTest::newRow("interval") << static_cast<int>(VideoFrameExtractionStrategy::IntervalOnly);
        QTest::newRow("scene-and-interval") << static_cast<int>(VideoFrameExtractionStrategy::SceneAndInterval);
        QTest::newRow("high-fidelity") << static_cast<int>(VideoFrameExtractionStrategy::HighFidelity);
    }

    void realFashionVideoCoversCompleteTimeline()
    {
        const auto source = qEnvironmentVariable("CINEVAULT_REAL_VIDEO_FIXTURE");
        if (source.isEmpty()) QSKIP("Provide the user-supplied 58.48-second fashion video via CINEVAULT_REAL_VIDEO_FIXTURE");
        QVERIFY(QFileInfo::exists(source));
        QFETCH(int, strategy);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FrameExtractionRequest request;
        request.sourcePath = source;
        request.outputDirectory = QDir(temp.path()).filePath(QStringLiteral("frames"));
        request.strategy = static_cast<VideoFrameExtractionStrategy>(strategy);
        request.intervalSeconds = 1.0;
        request.maxWidth = 320;
        request.maxHeight = 180;
        FFmpegAdapter adapter;
        const auto result = adapter.extractFrames(request);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.sourceFrameCount, 1462);
        QCOMPARE(result.frames.first().timestampMs, qint64{0});
        QCOMPARE(result.frames.last().timestampMs, qint64{58440});
        if (request.strategy == VideoFrameExtractionStrategy::PerFrame) QCOMPARE(result.frames.size(), 1462);
        else QVERIFY(result.frames.size() >= 59);
        for (int i = 1; i < result.frames.size(); ++i) {
            QVERIFY(result.frames.at(i).timestampMs > result.frames.at(i - 1).timestampMs);
            QVERIFY(result.frames.at(i).timestampMs - result.frames.at(i - 1).timestampMs <= 1040);
        }
        for (const auto index : {qsizetype{0}, result.frames.size() / 2, result.frames.size() - 1}) {
            const auto &frame = result.frames.at(index);
            const auto referencePath = QDir(temp.path()).filePath(QStringLiteral("reference-%1.jpg").arg(index));
            QString error;
            QVERIFY2(runProcess(m_ffmpegPath,
                {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-i"), source, QStringLiteral("-vf"),
                 QStringLiteral("select=eq(n\\,%1),scale=320:180,format=yuvj420p").arg(frame.timestampMs / 40),
                 QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
                 QStringLiteral("-frames:v"), QStringLiteral("1"),
                 QStringLiteral("-q:v"), QStringLiteral("2"), referencePath}, &error), qPrintable(error));
            const auto actual = QImage(frame.imagePath).convertToFormat(QImage::Format_RGB32);
            const auto expected = QImage(referencePath).convertToFormat(QImage::Format_RGB32);
            QVERIFY(!actual.isNull());
            QCOMPARE(actual.size(), expected.size());
            quint64 errorSum = 0;
            for (int y = 0; y < actual.height(); ++y) {
                const auto *a = reinterpret_cast<const QRgb *>(actual.constScanLine(y));
                const auto *b = reinterpret_cast<const QRgb *>(expected.constScanLine(y));
                for (int x = 0; x < actual.width(); ++x) {
                    errorSum += qAbs(qRed(a[x]) - qRed(b[x])) + qAbs(qGreen(a[x]) - qGreen(b[x]))
                        + qAbs(qBlue(a[x]) - qBlue(b[x]));
                }
            }
            const auto meanError = errorSum / (actual.width() * actual.height() * 3.0);
            QVERIFY2(meanError < 4.0, qPrintable(QStringLiteral("Frame at %1ms differs from actual source: MAE=%2")
                                                 .arg(frame.timestampMs).arg(meanError)));
        }
        qInfo() << "real-video strategy=" << strategy << "sample_count=" << result.frames.size()
                << "last_source_pts_ms=" << result.frames.last().timestampMs;
    }

    void cancellationStopsExternalProcessPromptly()
    {
        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto sourcePath = createVideo(
            temporaryDir,
            QStringLiteral("cancel.mkv"),
            QStringLiteral("testsrc=size=256x128:rate=30"),
            300);

        std::stop_source stopSource;
        stopSource.request_stop();
        FrameExtractionRequest request;
        request.sourcePath = sourcePath;
        request.outputDirectory = QDir(temporaryDir.path()).filePath(QStringLiteral("frames"));
        request.strategy = VideoFrameExtractionStrategy::PerFrame;
        request.stopToken = stopSource.get_token();
        FFmpegAdapter adapter;
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = adapter.extractFrames(request);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("取消")));
        QVERIFY2(elapsed.elapsed() < 2000,
                 qPrintable(QStringLiteral("取消耗时 %1ms").arg(elapsed.elapsed())));
    }

private:
    QString createVideo(QTemporaryDir &temporaryDir,
                        const QString &name,
                        const QString &filter,
                        int frameCount) const
    {
        const auto sourcePath = QDir(temporaryDir.path()).filePath(name);
        QString processError;
        const auto created = runProcess(
            m_ffmpegPath,
            {
                QStringLiteral("-y"),
                QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"), filter,
                QStringLiteral("-frames:v"), QString::number(frameCount),
                QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                sourcePath
            },
            &processError);
        if (!created) {
            QTest::qFail(qPrintable(processError), __FILE__, __LINE__);
            return {};
        }
        return sourcePath;
    }

    QString m_ffmpegPath;
    QString m_ffprobePath;
};

QTEST_GUILESS_MAIN(FFmpegAdapterFrameExtractionTest)

#include "FFmpegAdapterFrameExtractionTest.moc"
