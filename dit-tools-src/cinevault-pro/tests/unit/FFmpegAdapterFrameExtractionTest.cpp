#include "infrastructure/ffmpeg/FFmpegAdapter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

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
    void fixedInterval_includesAndRepairsTerminalFrame()
    {
        const auto ffmpegPath = existingExecutable(
            QStringLiteral("CINEVAULT_FFMPEG_PATH"),
            QStringLiteral("ffmpeg.exe"),
            {
                QStringLiteral("C:/Program Files/影资管家/ffmpeg/bin/ffmpeg.exe"),
                QStringLiteral("G:/data/app/DIT/ffmpeg/bin/ffmpeg.exe")
            });
        const auto ffprobePath = existingExecutable(
            QStringLiteral("CINEVAULT_FFPROBE_PATH"),
            QStringLiteral("ffprobe.exe"),
            {
                QStringLiteral("C:/Program Files/影资管家/ffmpeg/bin/ffprobe.exe"),
                QStringLiteral("G:/data/app/DIT/ffmpeg/bin/ffprobe.exe")
            });
        if (ffmpegPath.isEmpty() || ffprobePath.isEmpty()) {
            QSKIP("FFmpeg CLI runtime is unavailable");
        }

        qputenv("CINEVAULT_FFMPEG_PATH", ffmpegPath.toLocal8Bit());
        qputenv("CINEVAULT_FFPROBE_PATH", ffprobePath.toLocal8Bit());

        QTemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());
        const auto sourcePath = QDir(temporaryDir.path()).filePath(QStringLiteral("source.mkv"));
        QString processError;
        QVERIFY2(runProcess(
                     ffmpegPath,
                     {
                         QStringLiteral("-y"),
                         QStringLiteral("-v"), QStringLiteral("error"),
                         QStringLiteral("-f"), QStringLiteral("lavfi"),
                         QStringLiteral("-i"), QStringLiteral("testsrc=size=64x64:rate=10"),
                         QStringLiteral("-frames:v"), QStringLiteral("17"),
                         QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                         sourcePath
                     },
                     &processError),
                 qPrintable(processError));

        FFmpegAdapter adapter;
        QVERIFY2(adapter.isAvailable(), qPrintable(adapter.unavailableReason()));

        FrameExtractionRequest fullRequest;
        fullRequest.sourcePath = sourcePath;
        fullRequest.outputDirectory = QDir(temporaryDir.path()).filePath(QStringLiteral("full"));
        fullRequest.mode = AnalysisMode::CustomInterval;
        fullRequest.frameInterval = 15;
        fullRequest.maxWidth = 64;
        fullRequest.maxHeight = 64;

        const auto fullResult = adapter.extractFrames(fullRequest);
        QVERIFY2(fullResult.success, qPrintable(fullResult.errorMessage));
        QCOMPARE(fullResult.sourceFrameCount, 17);
        QCOMPARE(fullResult.frameInterval, 15);
        QCOMPARE(fullResult.frames.size(), 3);
        QCOMPARE(fullResult.frames.at(0).frameNumber, 1);
        QCOMPARE(fullResult.frames.at(1).frameNumber, 16);
        QCOMPARE(fullResult.frames.at(2).frameNumber, 17);
        for (const auto &frame : fullResult.frames) {
            const QFileInfo image(frame.imagePath);
            QVERIFY(image.isFile());
            QVERIFY(image.size() > 0);
        }

        FrameExtractionRequest repairRequest = fullRequest;
        repairRequest.outputDirectory = QDir(temporaryDir.path()).filePath(QStringLiteral("repair"));
        repairRequest.requestedFrameNumbers = {17};

        const auto repairResult = adapter.extractFrames(repairRequest);
        QVERIFY2(repairResult.success, qPrintable(repairResult.errorMessage));
        QCOMPARE(repairResult.frames.size(), 1);
        QCOMPARE(repairResult.frames.constFirst().frameNumber, 17);
        QVERIFY(QFileInfo(repairResult.frames.constFirst().imagePath).size() > 0);
    }
};

QTEST_GUILESS_MAIN(FFmpegAdapterFrameExtractionTest)

#include "FFmpegAdapterFrameExtractionTest.moc"
