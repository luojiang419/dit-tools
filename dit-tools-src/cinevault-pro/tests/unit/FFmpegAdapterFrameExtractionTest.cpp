#include "infrastructure/ffmpeg/FFmpegAdapter.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
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
