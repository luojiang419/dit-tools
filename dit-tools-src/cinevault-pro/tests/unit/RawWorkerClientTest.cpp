#include "infrastructure/raw/RawPreviewCacheKey.h"
#include "infrastructure/raw/RawPreviewProtocol.h"
#include "infrastructure/raw/RawWorkerClient.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTest>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <future>
#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool readExact(FILE *input, qsizetype size, QByteArray *data)
{
    data->resize(size);
    qsizetype offset = 0;
    while (offset < size) {
        const auto readSize = std::fread(data->data() + offset,
                                         1,
                                         static_cast<size_t>(size - offset),
                                         input);
        if (readSize == 0) {
            data->clear();
            return false;
        }
        offset += static_cast<qsizetype>(readSize);
    }
    return true;
}

bool readFixtureRequest(FILE *input, QJsonObject *request)
{
    QByteArray prefix;
    if (!readExact(input, 4, &prefix)) {
        return false;
    }
    const auto payloadSize = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(prefix.constData()));
    if (payloadSize == 0
        || payloadSize > static_cast<quint32>(RawPreviewProtocol::MaximumPayloadBytes)) {
        return false;
    }
    QByteArray payload;
    if (!readExact(input, payloadSize, &payload)) {
        return false;
    }
    auto frame = prefix + payload;
    return RawPreviewProtocol::tryTakeMessage(&frame, request)
        == RawPreviewProtocol::ReadStatus::MessageReady;
}

int runWorkerFixture()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int sequence = 0;
    while (true) {
        QJsonObject request;
        if (!readFixtureRequest(stdin, &request)) {
            return std::feof(stdin) ? 0 : 3;
        }
        const auto requestId = request.value(QStringLiteral("requestId")).toString();
        const auto command = request.value(QStringLiteral("command")).toString();
        const auto payload = request.value(QStringLiteral("payload")).toObject();
        if (command == QStringLiteral("hang")) {
            QThread::msleep(5000);
        } else if (command == QStringLiteral("delay")) {
            QThread::msleep(static_cast<unsigned long>(
                qBound(0, payload.value(QStringLiteral("milliseconds")).toInt(), 1000)));
        }

        ++sequence;
        const auto response = RawPreviewProtocol::successResponse(requestId, {
            {QStringLiteral("pid"), QString::number(QCoreApplication::applicationPid())},
            {QStringLiteral("sequence"), sequence},
        });
        const auto frame = RawPreviewProtocol::encodeMessage(response);
        if (std::fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout)
                != static_cast<size_t>(frame.size())
            || std::fflush(stdout) != 0) {
            return 4;
        }
    }
}

int runGprToolFixture(const QStringList &arguments)
{
    const auto outputIndex = arguments.indexOf(QStringLiteral("-o"));
    if (outputIndex < 0 || outputIndex + 1 >= arguments.size()) {
        return 2;
    }
    QImage image(1000, 750, QImage::Format_RGB888);
    image.fill(QColor(QStringLiteral("#2563eb")));
    for (int y = 0; y < image.height() / 2; ++y) {
        auto *scanLine = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            scanLine[(x * 3) + 1] = 180;
        }
    }
    QImageWriter writer(arguments.at(outputIndex + 1), "jpg");
    writer.setQuality(90);
    return writer.write(image) ? 0 : 3;
}

QString fixtureExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

} // namespace

class RawWorkerClientTest final : public QObject {
    Q_OBJECT

private slots:
    void protocolSupportsFragmentedFrames();
    void protocolRejectsOversizedFrames();
    void realWorkerAnswersPing();
    void concurrentCallsAreSerialized();
    void timeoutRestartsWorker();
    void cacheKeyTracksSourceFingerprintAndProfile();
    void gprSdkProviderProducesBoundedPreview();
    void fallbackProvidersProduceBoundedPreview();
    void invalidRawProducesPlaceholder();
};

void RawWorkerClientTest::protocolSupportsFragmentedFrames()
{
    const QJsonObject source = {
        {QStringLiteral("protocolVersion"), RawPreviewProtocol::ProtocolVersion},
        {QStringLiteral("requestId"), QStringLiteral("fragmented")},
    };
    const auto frame = RawPreviewProtocol::encodeMessage(source);
    QVERIFY(!frame.isEmpty());

    auto buffer = frame.first(3);
    QJsonObject decoded;
    QCOMPARE(RawPreviewProtocol::tryTakeMessage(&buffer, &decoded),
             RawPreviewProtocol::ReadStatus::NeedMoreData);
    buffer.append(frame.sliced(3));
    QCOMPARE(RawPreviewProtocol::tryTakeMessage(&buffer, &decoded),
             RawPreviewProtocol::ReadStatus::MessageReady);
    QCOMPARE(decoded, source);
    QVERIFY(buffer.isEmpty());
}

void RawWorkerClientTest::protocolRejectsOversizedFrames()
{
    auto buffer = QByteArray::fromHex("00100001");
    QJsonObject decoded;
    QString errorMessage;
    QCOMPARE(RawPreviewProtocol::tryTakeMessage(&buffer, &decoded, &errorMessage),
             RawPreviewProtocol::ReadStatus::InvalidFrame);
    QVERIFY(errorMessage.contains(QStringLiteral("越界")));
}

void RawWorkerClientTest::realWorkerAnswersPing()
{
    RawWorkerClient client(QString::fromUtf8(CINEVAULT_RAW_WORKER_TEST_PATH), {}, 3000);
    const auto reply = client.sendRequest(QStringLiteral("ping"));
    QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
    QCOMPARE(reply.result.value(QStringLiteral("workerVersion")).toString(),
             QStringLiteral("raw-worker-v1"));
    QCOMPARE(reply.result.value(QStringLiteral("protocolVersion")).toInt(),
             RawPreviewProtocol::ProtocolVersion);
    QVERIFY(reply.result.value(QStringLiteral("serial")).toBool());
}

void RawWorkerClientTest::concurrentCallsAreSerialized()
{
    RawWorkerClient client(fixtureExecutablePath(),
                           {QStringLiteral("--raw-worker-fixture")},
                           2000);
    QElapsedTimer elapsed;
    elapsed.start();
    auto first = std::async(std::launch::async, [&client]() {
        return client.sendRequest(QStringLiteral("delay"), {
            {QStringLiteral("milliseconds"), 150},
        });
    });
    QThread::msleep(20);
    auto second = std::async(std::launch::async, [&client]() {
        return client.sendRequest(QStringLiteral("delay"), {
            {QStringLiteral("milliseconds"), 150},
        });
    });

    const auto firstReply = first.get();
    const auto secondReply = second.get();
    QVERIFY2(firstReply.ok, qPrintable(firstReply.errorMessage));
    QVERIFY2(secondReply.ok, qPrintable(secondReply.errorMessage));
    QCOMPARE(firstReply.result.value(QStringLiteral("sequence")).toInt(), 1);
    QCOMPARE(secondReply.result.value(QStringLiteral("sequence")).toInt(), 2);
    QVERIFY2(elapsed.elapsed() >= 280,
             qPrintable(QStringLiteral("请求未串行执行：%1 ms").arg(elapsed.elapsed())));
}

void RawWorkerClientTest::timeoutRestartsWorker()
{
    RawWorkerClient client(fixtureExecutablePath(),
                           {QStringLiteral("--raw-worker-fixture")},
                           200);
    const auto before = client.sendRequest(QStringLiteral("ping"));
    QVERIFY2(before.ok, qPrintable(before.errorMessage));
    const auto oldPid = before.result.value(QStringLiteral("pid")).toString();
    QVERIFY(!oldPid.isEmpty());

    const auto timeout = client.sendRequest(QStringLiteral("hang"));
    QVERIFY(!timeout.ok);
    QCOMPARE(timeout.errorCode, QStringLiteral("timeout"));
    QVERIFY(timeout.retryable);
    QVERIFY(client.restartCount() >= 1);

    const auto after = client.sendRequest(QStringLiteral("ping"));
    QVERIFY2(after.ok, qPrintable(after.errorMessage));
    const auto newPid = after.result.value(QStringLiteral("pid")).toString();
    QVERIFY(!newPid.isEmpty());
    QVERIFY2(newPid != oldPid,
             qPrintable(QStringLiteral("worker 未重启：PID 仍为 %1").arg(oldPid)));
}

void RawWorkerClientTest::cacheKeyTracksSourceFingerprintAndProfile()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const auto sourcePath = QDir(temporaryDir.path()).filePath(QStringLiteral("样片.ARW"));
    const auto baseCachePath = QDir(temporaryDir.path()).filePath(
        QStringLiteral("cache/preview.jpg"));

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("a", 1), 1);
    source.close();
    const auto first = RawPreviewCacheKey::fromSource(sourcePath, baseCachePath, 480);

    QVERIFY(source.open(QIODevice::Append));
    QCOMPARE(source.write("b", 1), 1);
    source.close();
    const auto second = RawPreviewCacheKey::fromSource(sourcePath, baseCachePath, 480);

    QCOMPARE(first.decoderPackageVersion,
             QStringLiteral("libraw-0.22.2+gpr-sdk-446c736"));
    QCOMPARE(first.profileVersion, QStringLiteral("raw-preview-v1"));
    QCOMPARE(first.generatorProfile,
             QStringLiteral("raw-preview-v1/libraw-0.22.2+gpr-sdk-446c736"));
    QCOMPARE(first.cacheKey.size(), 64);
    QVERIFY(first.cacheKey != second.cacheKey);
    QVERIFY(first.outputPath != second.outputPath);
    QVERIFY(first.outputPath.contains(QStringLiteral(".raw-")));
    QVERIFY(first.outputPath.endsWith(QStringLiteral(".jpg")));
}

void RawWorkerClientTest::fallbackProvidersProduceBoundedPreview()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const auto sourcePath = QDir(temporaryDir.path()).filePath(QStringLiteral("相机样片.dng"));
    QImage source(1600, 900, QImage::Format_RGB32);
    source.fill(QColor(QStringLiteral("#3b82f6")));
    QImageWriter sourceWriter(sourcePath, "jpg");
    sourceWriter.setQuality(95);
    QVERIFY2(sourceWriter.write(source), qPrintable(sourceWriter.errorString()));

    const auto outputPath = QDir(temporaryDir.path())
                                .filePath(QStringLiteral("cache/preview.jpg"));
    RawWorkerClient client(QString::fromUtf8(CINEVAULT_RAW_WORKER_TEST_PATH), {}, 20000);
    const auto reply = client.decode({
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("baseCachePath"), outputPath},
        {QStringLiteral("maxEdge"), 480},
    });
    QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
    QVERIFY(!reply.result.value(QStringLiteral("placeholder")).toBool());
    QVERIFY(reply.result.value(QStringLiteral("provider")).toString()
            == QStringLiteral("wic")
        || reply.result.value(QStringLiteral("provider")).toString()
            == QStringLiteral("ffmpeg"));

    const auto actualOutputPath = reply.result.value(QStringLiteral("outputPath")).toString();
    QVERIFY(!actualOutputPath.isEmpty());
    QVERIFY(actualOutputPath != outputPath);
    QVERIFY(actualOutputPath.contains(QStringLiteral(".raw-")));
    QCOMPARE(reply.result.value(QStringLiteral("cacheKey")).toString().size(), 64);
    QCOMPARE(reply.result.value(QStringLiteral("decoderPackageVersion")).toString(),
             QStringLiteral("libraw-0.22.2+gpr-sdk-446c736"));
    QCOMPARE(reply.result.value(QStringLiteral("profileVersion")).toString(),
             QStringLiteral("raw-preview-v1"));

    QImageReader reader(actualOutputPath, "jpg");
    const auto preview = reader.read();
    QVERIFY2(!preview.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(qMax(preview.width(), preview.height()), 480);
    QVERIFY(qAbs((preview.width() / static_cast<double>(preview.height()))
                 - (16.0 / 9.0)) < 0.02);
    QCOMPARE(preview.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(!QFileInfo::exists(actualOutputPath + QStringLiteral(".tmp")));

    const auto attempts = reply.result.value(QStringLiteral("attempts")).toArray();
    QVERIFY(attempts.size() >= 3);
    QCOMPARE(attempts.at(0).toObject().value(QStringLiteral("provider")).toString(),
             QStringLiteral("libraw_embedded"));
    QCOMPARE(attempts.at(1).toObject().value(QStringLiteral("provider")).toString(),
             QStringLiteral("libraw_rendered"));
    QCOMPARE(attempts.at(2).toObject().value(QStringLiteral("provider")).toString(),
             QStringLiteral("exiftool_embedded"));

    const auto cachedReply = client.decode({
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("baseCachePath"), outputPath},
        {QStringLiteral("maxEdge"), 480},
    });
    QVERIFY2(cachedReply.ok, qPrintable(cachedReply.errorMessage));
    QCOMPARE(cachedReply.result.value(QStringLiteral("provider")).toString(),
             QStringLiteral("cache"));
    QCOMPARE(cachedReply.result.value(QStringLiteral("outputPath")).toString(),
             actualOutputPath);
    QCOMPARE(cachedReply.result.value(QStringLiteral("cacheKey")).toString(),
             reply.result.value(QStringLiteral("cacheKey")).toString());
}

void RawWorkerClientTest::gprSdkProviderProducesBoundedPreview()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const auto sourcePath = QDir(temporaryDir.path()).filePath(QStringLiteral("样片.GPR"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("mock-gpr-container", 18), 18);
    source.close();

    const auto previousGprToolsPath = qgetenv("CINEVAULT_GPR_TOOLS_PATH");
    const auto restoreEnvironment = qScopeGuard([previousGprToolsPath]() {
        if (previousGprToolsPath.isNull()) {
            qunsetenv("CINEVAULT_GPR_TOOLS_PATH");
        } else {
            qputenv("CINEVAULT_GPR_TOOLS_PATH", previousGprToolsPath);
        }
    });
    qputenv("CINEVAULT_GPR_TOOLS_PATH", QFile::encodeName(fixtureExecutablePath()));

    const auto baseCachePath = QDir(temporaryDir.path()).filePath(
        QStringLiteral("cache/gpr-preview.jpg"));
    RawWorkerClient client(QString::fromUtf8(CINEVAULT_RAW_WORKER_TEST_PATH), {}, 20000);
    const auto reply = client.decode({
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("baseCachePath"), baseCachePath},
        {QStringLiteral("maxEdge"), 480},
    });
    QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
    QCOMPARE(reply.result.value(QStringLiteral("provider")).toString(),
             QStringLiteral("gopro_gpr_sdk"));
    QVERIFY(!reply.result.value(QStringLiteral("placeholder")).toBool());
    QCOMPARE(reply.result.value(QStringLiteral("width")).toInt(), 480);
    QCOMPARE(reply.result.value(QStringLiteral("height")).toInt(), 360);
    QVERIFY(reply.result.value(QStringLiteral("attempts")).toArray().isEmpty());

    QImageReader reader(reply.result.value(QStringLiteral("outputPath")).toString(), "jpg");
    const auto preview = reader.read();
    QVERIFY2(!preview.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(preview.size(), QSize(480, 360));

    const auto cachedReply = client.decode({
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("baseCachePath"), baseCachePath},
        {QStringLiteral("maxEdge"), 480},
    });
    QVERIFY2(cachedReply.ok, qPrintable(cachedReply.errorMessage));
    QCOMPARE(cachedReply.result.value(QStringLiteral("provider")).toString(),
             QStringLiteral("cache"));
}

void RawWorkerClientTest::invalidRawProducesPlaceholder()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const auto sourcePath = QDir(temporaryDir.path()).filePath(QStringLiteral("损坏样片.NEF"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("not-a-valid-raw", 15), 15);
    source.close();

    const auto outputPath = QDir(temporaryDir.path()).filePath(QStringLiteral("placeholder.jpg"));
    RawWorkerClient client(QString::fromUtf8(CINEVAULT_RAW_WORKER_TEST_PATH), {}, 20000);
    const auto placeholderOnlyPath = QDir(temporaryDir.path())
                                         .filePath(QStringLiteral("placeholder-only.jpg"));
    const auto placeholderOnly = client.sendRequest(QStringLiteral("decode"), {
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("baseCachePath"), placeholderOnlyPath},
        {QStringLiteral("maxEdge"), 480},
        {QStringLiteral("providerStartIndex"), 5},
    });
    QVERIFY2(placeholderOnly.ok, qPrintable(placeholderOnly.errorMessage));
    QVERIFY(placeholderOnly.result.value(QStringLiteral("placeholder")).toBool());
    const auto reply = client.decode({
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("baseCachePath"), outputPath},
        {QStringLiteral("maxEdge"), 480},
    });
    QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
    QVERIFY(reply.result.value(QStringLiteral("placeholder")).toBool());
    QCOMPARE(reply.result.value(QStringLiteral("provider")).toString(),
             QStringLiteral("placeholder"));
    QVERIFY(QFileInfo(reply.result.value(QStringLiteral("outputPath")).toString()).size() > 0);
    QVERIFY(reply.result.value(QStringLiteral("attempts")).toArray().size() >= 5);
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--raw-worker-fixture"))) {
        return runWorkerFixture();
    }
    if (application.arguments().contains(QStringLiteral("-i"))
        && application.arguments().contains(QStringLiteral("-o"))
        && application.arguments().contains(QStringLiteral("-r"))) {
        return runGprToolFixture(application.arguments());
    }
    RawWorkerClientTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "RawWorkerClientTest.moc"
