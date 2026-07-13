#include <QtTest>

#include "infrastructure/network/VisionApiClient.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace {
struct ChatCompletionResponse {
    int statusCode = 200;
    QByteArray body;
};

QByteArray sampleWebp()
{
    return QByteArray::fromBase64("UklGRjAAAABXRUJQVlA4TCMAAAAvAUAAEB8gEEjeHzqN+RcQFPwfnYCg6LrlImYPwg0YIvofAgA=");
}

QByteArray summaryChatResponse()
{
    const QJsonObject summary{
        {QStringLiteral("summary"), QStringLiteral("WebP 测试图片摘要")},
        {QStringLiteral("keywords"), QJsonArray{QStringLiteral("WebP"), QStringLiteral("测试图片")}},
        {QStringLiteral("scenes"), QJsonArray{QStringLiteral("测试场景")}},
    };
    const QJsonObject root{
        {QStringLiteral("choices"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("message"),
                  QJsonObject{
                      {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Compact))}
                  }}
             }
         }}
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray dimensionChatResponse()
{
    const QJsonObject payload{
        {QStringLiteral("dimensions"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("色彩风格")},
                 {QStringLiteral("detail"), QStringLiteral("冷蓝色调，标题对比明确。")}
             }
         }}
    };
    const QJsonObject root{
        {QStringLiteral("choices"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("message"),
                  QJsonObject{
                      {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}
                  }}
             }
         }}
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

qsizetype contentLengthFromHeader(const QByteArray &header)
{
    const auto lines = header.split('\n');
    for (auto line : lines) {
        line = line.trimmed();
        const QByteArray prefix("content-length:");
        if (!line.toLower().startsWith(prefix)) {
            continue;
        }
        bool ok = false;
        const auto length = line.mid(prefix.size()).trimmed().toLongLong(&ok);
        return ok ? static_cast<qsizetype>(length) : 0;
    }
    return 0;
}

QByteArray reasonPhrase(int statusCode)
{
    if (statusCode == 400) {
        return QByteArray("Bad Request");
    }
    return QByteArray("OK");
}

void installChatCompletionResponder(QTcpServer *server, QByteArray *capturedBody, const QByteArray &responseBody)
{
    QObject::connect(server, &QTcpServer::newConnection, server, [server, capturedBody, responseBody]() {
        auto *socket = server->nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, capturedBody, responseBody, request = QByteArray()]() mutable {
            request.append(socket->readAll());
            const auto headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }

            const auto bodyStart = headerEnd + 4;
            const auto expectedLength = contentLengthFromHeader(request.left(headerEnd));
            if (request.size() < bodyStart + expectedLength) {
                return;
            }

            *capturedBody = request.mid(bodyStart, expectedLength);
            const auto responseHeader = QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                + QByteArray::number(responseBody.size())
                + QByteArray("\r\nConnection: close\r\n\r\n");
            socket->write(responseHeader);
            socket->write(responseBody);
            socket->disconnectFromHost();
        });
    });
}

void installSequentialChatCompletionResponder(QTcpServer *server,
                                              QVector<QByteArray> *capturedBodies,
                                              const QVector<ChatCompletionResponse> &responses)
{
    const auto sharedResponses = QSharedPointer<QVector<ChatCompletionResponse>>::create(responses);
    const auto nextIndex = QSharedPointer<int>::create(0);
    QObject::connect(server, &QTcpServer::newConnection, server, [server, capturedBodies, sharedResponses, nextIndex]() {
        auto *socket = server->nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, capturedBodies, sharedResponses, nextIndex, request = QByteArray()]() mutable {
            request.append(socket->readAll());
            const auto headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }

            const auto bodyStart = headerEnd + 4;
            const auto expectedLength = contentLengthFromHeader(request.left(headerEnd));
            if (request.size() < bodyStart + expectedLength) {
                return;
            }

            capturedBodies->append(request.mid(bodyStart, expectedLength));
            const auto lastResponseIndex = qMax(0, static_cast<int>(sharedResponses->size()) - 1);
            const auto responseIndex = qMin((*nextIndex)++, lastResponseIndex);
            const auto response = sharedResponses->at(responseIndex);
            const auto responseHeader = QByteArray("HTTP/1.1 ")
                + QByteArray::number(response.statusCode)
                + QByteArray(" ")
                + reasonPhrase(response.statusCode)
                + QByteArray("\r\nContent-Type: application/json\r\nContent-Length: ")
                + QByteArray::number(response.body.size())
                + QByteArray("\r\nConnection: close\r\n\r\n");
            socket->write(responseHeader);
            socket->write(response.body);
            socket->disconnectFromHost();
        });
    });
}

QString promptTextFromRequestBody(const QByteArray &body)
{
    QJsonParseError parseError;
    const auto requestDocument = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }
    return requestDocument.object()
        .value(QStringLiteral("messages")).toArray().first().toObject()
        .value(QStringLiteral("content")).toArray().first().toObject()
        .value(QStringLiteral("text")).toString();
}

QJsonObject responseFormatFromRequestBody(const QByteArray &body)
{
    QJsonParseError parseError;
    const auto requestDocument = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }
    return requestDocument.object().value(QStringLiteral("response_format")).toObject();
}
}

class VisionApiClientImageTest : public QObject {
    Q_OBJECT

private slots:
    void analyzeImage_decodesWebpToJpegDataUrl();
    void analyzeFrameDimensions_postsSingleFrameImage();
    void analyzeDimensions_postsRequestedDimensions();
    void analyzeDimensions_retriesWithShorterContextOnContextLimit();
    void analyzeDimensions_fallsBackToTextWhenResponseFormatRejected();
};

void VisionApiClientImageTest::analyzeImage_decodesWebpToJpegDataUrl()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "temporary directory should be created");

    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("sample.webp"));
    QFile imageFile(imagePath);
    QVERIFY2(imageFile.open(QIODevice::WriteOnly), "sample webp should be writable");
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QByteArray capturedBody;
    installChatCompletionResponder(&server, &capturedBody, summaryChatResponse());

    VisionApiClient client;
    QString error;
    int httpStatusCode = 0;
    const auto summary = client.analyzeImage(imagePath,
                                             QStringLiteral("Brand_2026_Shanghai_sample.webp"),
                                             QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
                                             QStringLiteral("test-key"),
                                             QStringLiteral("test-model"),
                                             5,
                                             &error,
                                             &httpStatusCode);

    QVERIFY2(summary.has_value(), qPrintable(error));
    QCOMPARE(httpStatusCode, 200);
    QCOMPARE(summary->summary, QStringLiteral("WebP 测试图片摘要"));

    QJsonParseError parseError;
    const auto requestDocument = QJsonDocument::fromJson(capturedBody, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const auto responseFormat = requestDocument.object().value(QStringLiteral("response_format")).toObject();
    QCOMPARE(responseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
    QCOMPARE(responseFormat.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("vision_video_summary"));
    const auto content = requestDocument.object()
                             .value(QStringLiteral("messages")).toArray().first().toObject()
                             .value(QStringLiteral("content")).toArray();
    const auto promptText = content.at(0).toObject().value(QStringLiteral("text")).toString();
    QVERIFY(promptText.contains(QStringLiteral("Brand_2026_Shanghai_sample.webp")));
    const auto imageUrl = content.at(1).toObject()
                              .value(QStringLiteral("image_url")).toObject()
                              .value(QStringLiteral("url")).toString();
    QVERIFY(imageUrl.startsWith(QStringLiteral("data:image/jpeg;base64,")));

    const auto encoded = imageUrl.mid(QStringLiteral("data:image/jpeg;base64,").size()).toLatin1();
    const auto jpegBytes = QByteArray::fromBase64(encoded);
    QVERIFY(jpegBytes.startsWith(QByteArray::fromHex("ffd8")));
}

void VisionApiClientImageTest::analyzeFrameDimensions_postsSingleFrameImage()
{
    QTemporaryDir tempDir;
    QVERIFY2(tempDir.isValid(), "temporary directory should be created");

    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("frame.webp"));
    QFile imageFile(imagePath);
    QVERIFY2(imageFile.open(QIODevice::WriteOnly), "sample webp should be writable");
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QByteArray capturedBody;
    installChatCompletionResponder(&server, &capturedBody, dimensionChatResponse());

    VisionApiClient client;
    QString error;
    int httpStatusCode = 0;
    const auto analyses = client.analyzeFrameDimensions(imagePath,
                                                        QStringLiteral("clip.mov"),
                                                        QStringLiteral("第 12 帧：描述：蓝色标题出现在屏幕中央。"),
                                                        QStringList{QStringLiteral("色彩风格")},
                                                        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
                                                        QStringLiteral("test-key"),
                                                        QStringLiteral("test-model"),
                                                        5,
                                                        &error,
                                                        &httpStatusCode);

    QVERIFY2(analyses.has_value(), qPrintable(error));
    QCOMPARE(httpStatusCode, 200);
    QCOMPARE(analyses->size(), 1);

    QJsonParseError parseError;
    const auto requestDocument = QJsonDocument::fromJson(capturedBody, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const auto responseFormat = requestDocument.object().value(QStringLiteral("response_format")).toObject();
    QCOMPARE(responseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
    QCOMPARE(responseFormat.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("vision_dimension_analysis"));
    const auto content = requestDocument.object()
                             .value(QStringLiteral("messages")).toArray().first().toObject()
                             .value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 2);
    const auto promptText = content.at(0).toObject().value(QStringLiteral("text")).toString();
    QVERIFY(promptText.contains(QStringLiteral("clip.mov")));
    QVERIFY(promptText.contains(QStringLiteral("第 12 帧")));
    QVERIFY(promptText.contains(QStringLiteral("色彩风格")));
    const auto imageUrl = content.at(1).toObject()
                              .value(QStringLiteral("image_url")).toObject()
                              .value(QStringLiteral("url")).toString();
    QVERIFY(imageUrl.startsWith(QStringLiteral("data:image/jpeg;base64,")));
}

void VisionApiClientImageTest::analyzeDimensions_postsRequestedDimensions()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QByteArray capturedBody;
    installChatCompletionResponder(&server, &capturedBody, dimensionChatResponse());

    VisionApiClient client;
    QString error;
    int httpStatusCode = 0;
    const auto analyses = client.analyzeDimensions(QStringLiteral("poster.webp"),
                                                   QStringLiteral("基础摘要：海报包含蓝色标题和品牌标签。"),
                                                   QStringList{QStringLiteral("色彩风格")},
                                                   QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
                                                   QStringLiteral("test-key"),
                                                   QStringLiteral("test-model"),
                                                   5,
                                                   &error,
                                                   &httpStatusCode);

    QVERIFY2(analyses.has_value(), qPrintable(error));
    QCOMPARE(httpStatusCode, 200);
    QCOMPARE(analyses->size(), 1);
    QCOMPARE(analyses->first().name, QStringLiteral("色彩风格"));
    QVERIFY(analyses->first().detail.contains(QStringLiteral("冷蓝色调")));

    QJsonParseError parseError;
    const auto requestDocument = QJsonDocument::fromJson(capturedBody, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const auto promptText = requestDocument.object()
                               .value(QStringLiteral("messages")).toArray().first().toObject()
                               .value(QStringLiteral("content")).toArray().first().toObject()
                               .value(QStringLiteral("text")).toString();
    const auto responseFormat = requestDocument.object().value(QStringLiteral("response_format")).toObject();
    QCOMPARE(responseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
    QCOMPARE(responseFormat.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("vision_dimension_analysis"));
    QVERIFY(promptText.contains(QStringLiteral("poster.webp")));
    QVERIFY(promptText.contains(QStringLiteral("色彩风格")));
    QVERIFY(promptText.contains(QStringLiteral("海报包含蓝色标题")));
}

void VisionApiClientImageTest::analyzeDimensions_retriesWithShorterContextOnContextLimit()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(&server,
                                             &capturedBodies,
                                             QVector<ChatCompletionResponse>{
                                                 {400, QByteArray(R"({"error":"Context size has been exceeded."})")},
                                                 {200, dimensionChatResponse()},
                                             });

    VisionApiClient client;
    QString error;
    int httpStatusCode = 0;
    const auto longContext = QStringLiteral("基础摘要：")
        + QString(20000, QLatin1Char('A'))
        + QStringLiteral("TAIL_MARKER_SHOULD_BE_CLIPPED");
    const auto analyses = client.analyzeDimensions(QStringLiteral("long-video.mov"),
                                                   longContext,
                                                   QStringList{QStringLiteral("美术风格")},
                                                   QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
                                                   QStringLiteral("test-key"),
                                                   QStringLiteral("test-model"),
                                                   5,
                                                   &error,
                                                   &httpStatusCode);

    QVERIFY2(analyses.has_value(), qPrintable(error));
    QCOMPARE(httpStatusCode, 200);
    QCOMPARE(analyses->size(), 1);
    QCOMPARE(capturedBodies.size(), 2);

    const auto firstPrompt = promptTextFromRequestBody(capturedBodies.first());
    const auto secondPrompt = promptTextFromRequestBody(capturedBodies.at(1));
    QVERIFY(!firstPrompt.contains(QStringLiteral("TAIL_MARKER_SHOULD_BE_CLIPPED")));
    QVERIFY(secondPrompt.size() < firstPrompt.size());
}

void VisionApiClientImageTest::analyzeDimensions_fallsBackToTextWhenResponseFormatRejected()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(&server,
                                             &capturedBodies,
                                             QVector<ChatCompletionResponse>{
                                                 {400, QByteArray(R"({"error":"'response_format.type' must be 'json_schema' or 'text'"})")},
                                                 {200, dimensionChatResponse()},
                                             });

    VisionApiClient client;
    QString error;
    int httpStatusCode = 0;
    const auto analyses = client.analyzeDimensions(QStringLiteral("poster.webp"),
                                                   QStringLiteral("基础摘要：海报包含蓝色标题和品牌标签。"),
                                                   QStringList{QStringLiteral("色彩风格")},
                                                   QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
                                                   QStringLiteral("test-key"),
                                                   QStringLiteral("test-model"),
                                                   5,
                                                   &error,
                                                   &httpStatusCode);

    QVERIFY2(analyses.has_value(), qPrintable(error));
    QCOMPARE(httpStatusCode, 200);
    QCOMPARE(capturedBodies.size(), 2);

    const auto firstResponseFormat = responseFormatFromRequestBody(capturedBodies.first());
    QCOMPARE(firstResponseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
    QCOMPARE(firstResponseFormat.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("vision_dimension_analysis"));

    const auto secondResponseFormat = responseFormatFromRequestBody(capturedBodies.at(1));
    QCOMPARE(secondResponseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
}

QTEST_MAIN(VisionApiClientImageTest)

#include "VisionApiClientImageTest.moc"
