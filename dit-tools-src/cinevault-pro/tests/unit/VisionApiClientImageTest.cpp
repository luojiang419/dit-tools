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

#include <stop_token>

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

QByteArray structuredFrameChatResponse()
{
    const QJsonObject frame{
        {QStringLiteral("caption"), QStringLiteral("红色牛仔短裤旁有促销文字")},
        {QStringLiteral("detail"), QStringLiteral("红色牛仔短裤居中展示，侧面有促销文字")},
        {QStringLiteral("scene"), QStringLiteral("服装商店")},
        {QStringLiteral("props"), QStringLiteral("红色牛仔短裤、促销牌")},
        {QStringLiteral("people"), QStringLiteral("不适用")},
        {QStringLiteral("expression"), QStringLiteral("不适用")},
        {QStringLiteral("body_action"), QStringLiteral("静态展示")},
        {QStringLiteral("movement_trend"), QStringLiteral("无明显运动")},
        {QStringLiteral("camera_movement"), QStringLiteral("固定镜头")},
        {QStringLiteral("shot_size"), QStringLiteral("近景")},
        {QStringLiteral("composition"), QStringLiteral("居中构图")},
        {QStringLiteral("subject_direction"), QStringLiteral("正面")},
        {QStringLiteral("gaze_direction"), QStringLiteral("不适用")},
        {QStringLiteral("action_stage"), QStringLiteral("静态")},
        {QStringLiteral("spatial_relation"), QStringLiteral("促销牌位于短裤右侧")},
        {QStringLiteral("chronology_cue"), QStringLiteral("不明显")},
        {QStringLiteral("camera_angle"), QStringLiteral("平视")},
        {QStringLiteral("visual_focus"), QStringLiteral("红色牛仔短裤")},
        {QStringLiteral("lighting_mood"), QStringLiteral("明亮商业光")},
        {QStringLiteral("color_palette"), QStringLiteral("红色、白色")},
        {QStringLiteral("narrative_function"), QStringLiteral("广告产品记忆点")},
        {QStringLiteral("transition_hint"), QStringLiteral("承接产品细节特写")}
    };
    const QJsonObject root{
        {QStringLiteral("choices"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("message"),
                  QJsonObject{
                      {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact))}
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

QByteArray chatResponseForPayload(const QJsonObject &payload)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("choices"), QJsonArray{QJsonObject{
            {QStringLiteral("message"), QJsonObject{
                {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}
            }}
        }}}
    }).toJson(QJsonDocument::Compact);
}

QByteArray emptyReasoningChatResponse()
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("choices"), QJsonArray{QJsonObject{
            {QStringLiteral("finish_reason"), QStringLiteral("length")},
            {QStringLiteral("message"), QJsonObject{
                {QStringLiteral("content"), QString()},
                {QStringLiteral("reasoning_content"), QStringLiteral("reasoning only")}
            }}
        }}},
        {QStringLiteral("usage"), QJsonObject{
            {QStringLiteral("prompt_tokens"), 100},
            {QStringLiteral("completion_tokens"), 640},
            {QStringLiteral("total_tokens"), 740}
        }}
    }).toJson(QJsonDocument::Compact);
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
    if (statusCode == 500) {
        return QByteArray("Internal Server Error");
    }
    if (statusCode == 502) {
        return QByteArray("Bad Gateway");
    }
    if (statusCode == 503) {
        return QByteArray("Service Unavailable");
    }
    if (statusCode == 504) {
        return QByteArray("Gateway Timeout");
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
    void analyzeFrame_requestsStoryboardSchema();
    void analyzeFrame_retriesOriginalRequestOnceAfterEmptyContent();
    void analyzeFrame_stopsAfterSingleEmptyContentRetry();
    void analyzeFrame_doesNotRepairMiniMaxBusinessError();
    void analyzeFrameDimensions_postsSingleFrameImage();
    void analyzeDimensions_postsRequestedDimensions();
    void analyzeDimensions_retriesWithShorterContextOnContextLimit();
    void analyzeDimensions_fallsBackToTextWhenResponseFormatRejected();
    void testConnection_retriesTransientGatewayFailure();
    void testConnection_stopsAfterBoundedGatewayRetries();
    void endpointPolicy_defaultsRemoteToHttpsAndAllowsExplicitHttp();
    void miniMaxM3_usesNativeEndpointAndFrameConcurrency();
    void analyzeFrame_honorsPreCancelledToken();
};

void VisionApiClientImageTest::analyzeFrame_honorsPreCancelledToken()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("cancelled-frame.webp"));
    QFile imageFile(imagePath);
    QVERIFY(imageFile.open(QIODevice::WriteOnly));
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    std::stop_source stopSource;
    stopSource.request_stop();
    VisionApiClient client;
    QString error;
    int statusCode = 0;
    const auto result = client.analyzeFrame(imagePath,
                                            QStringLiteral("cancelled.mov"),
                                            QStringLiteral("http://127.0.0.1:1/v1"),
                                            QStringLiteral("test-key"),
                                            QStringLiteral("test-model"),
                                            5,
                                            &error,
                                            &statusCode,
                                            stopSource.get_token());
    QVERIFY(!result.has_value());
    QCOMPARE(statusCode, 0);
    QCOMPARE(error, QStringLiteral("请求已取消"));
}

void VisionApiClientImageTest::analyzeFrame_requestsStoryboardSchema()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("frame.webp"));
    QFile imageFile(imagePath);
    QVERIFY(imageFile.open(QIODevice::WriteOnly));
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QByteArray capturedBody;
    installChatCompletionResponder(&server, &capturedBody, structuredFrameChatResponse());

    VisionApiClient client;
    QString error;
    int httpStatusCode = 0;
    const auto frame = client.analyzeFrame(imagePath,
                                           QStringLiteral("shop.mov"),
                                           QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
                                           QStringLiteral("test-key"),
                                           QStringLiteral("test-model"),
                                           5,
                                           &error,
                                           &httpStatusCode);

    QVERIFY2(frame.has_value(), qPrintable(error));
    QCOMPARE(httpStatusCode, 200);
    QVERIFY(frame->factsComplete);
    QVERIFY(frame->tags.contains(QStringLiteral("景别：近景")));
    QVERIFY(frame->objects.contains(QStringLiteral("红色牛仔短裤、促销牌")));
    QVERIFY(frame->actions.contains(QStringLiteral("固定镜头")));

    const auto responseFormat = responseFormatFromRequestBody(capturedBody);
    QJsonParseError requestParseError;
    const auto request = QJsonDocument::fromJson(capturedBody, &requestParseError).object();
    QCOMPARE(requestParseError.error, QJsonParseError::NoError);
    QCOMPARE(request.value(QStringLiteral("temperature")).toInt(), 0);
    QCOMPARE(request.value(QStringLiteral("max_tokens")).toInt(), 640);
    QCOMPARE(request.value(QStringLiteral("chat_template_kwargs")).toObject()
                 .value(QStringLiteral("enable_thinking")).toBool(), false);
    const auto schema = responseFormat.value(QStringLiteral("json_schema")).toObject()
                            .value(QStringLiteral("schema")).toObject();
    const auto required = schema.value(QStringLiteral("required")).toArray();
    QCOMPARE(required.size(), 22);
    QVERIFY(required.contains(QStringLiteral("detail")));
    QVERIFY(required.contains(QStringLiteral("camera_movement")));
    QVERIFY(required.contains(QStringLiteral("transition_hint")));
    const auto prompt = promptTextFromRequestBody(capturedBody);
    QVERIFY(prompt.contains(QStringLiteral("故事板")));
    QVERIFY(prompt.contains(QStringLiteral("transition_hint")));
}

void VisionApiClientImageTest::analyzeFrame_retriesOriginalRequestOnceAfterEmptyContent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("retry-frame.webp"));
    QFile imageFile(imagePath);
    QVERIFY(imageFile.open(QIODevice::WriteOnly));
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(
        &server,
        &capturedBodies,
        {
            {200, emptyReasoningChatResponse()},
            {200, structuredFrameChatResponse()}
        });

    VisionApiClient client;
    QString error;
    const auto frame = client.analyzeFrame(
        imagePath,
        QStringLiteral("retry.mov"),
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
        QStringLiteral("test-key"),
        QStringLiteral("test-model"),
        5,
        &error);

    QVERIFY2(frame.has_value(), qPrintable(error));
    QCOMPARE(capturedBodies.size(), 2);
    QJsonParseError firstParseError;
    QJsonParseError secondParseError;
    const auto firstRequest = QJsonDocument::fromJson(
        capturedBodies.first(), &firstParseError).object();
    const auto secondRequest = QJsonDocument::fromJson(
        capturedBodies.at(1), &secondParseError).object();
    QCOMPARE(firstParseError.error, QJsonParseError::NoError);
    QCOMPARE(secondParseError.error, QJsonParseError::NoError);
    QCOMPARE(firstRequest.value(QStringLiteral("messages")),
             secondRequest.value(QStringLiteral("messages")));
    QCOMPARE(firstRequest.value(QStringLiteral("max_tokens")).toInt(), 640);
    QCOMPARE(secondRequest.value(QStringLiteral("max_tokens")).toInt(), 1280);
    QCOMPARE(secondRequest.value(QStringLiteral("chat_template_kwargs")).toObject()
                 .value(QStringLiteral("enable_thinking")).toBool(), false);
}

void VisionApiClientImageTest::analyzeFrame_stopsAfterSingleEmptyContentRetry()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("empty-frame.webp"));
    QFile imageFile(imagePath);
    QVERIFY(imageFile.open(QIODevice::WriteOnly));
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(
        &server,
        &capturedBodies,
        {
            {200, emptyReasoningChatResponse()},
            {200, emptyReasoningChatResponse()}
        });

    VisionApiClient client;
    QString error;
    const auto frame = client.analyzeFrame(
        imagePath,
        QStringLiteral("empty.mov"),
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
        QStringLiteral("test-key"),
        QStringLiteral("test-model"),
        5,
        &error);

    QVERIFY(!frame.has_value());
    QCOMPARE(capturedBodies.size(), 2);
    QVERIFY(error.contains(QStringLiteral("token 上限")));
    QVERIFY(error.contains(QStringLiteral("仅返回推理内容")));
    QVERIFY(!error.contains(QStringLiteral("自动修复失败")));
    QVERIFY(!error.contains(QStringLiteral("纯文本兜底失败")));
}

void VisionApiClientImageTest::analyzeFrame_doesNotRepairMiniMaxBusinessError()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto imagePath = QDir(tempDir.path()).filePath(QStringLiteral("minimax-error-frame.webp"));
    QFile imageFile(imagePath);
    QVERIFY(imageFile.open(QIODevice::WriteOnly));
    QCOMPARE(imageFile.write(sampleWebp()), sampleWebp().size());
    imageFile.close();

    const auto businessErrorResponse = QJsonDocument(QJsonObject{
        {QStringLiteral("base_resp"), QJsonObject{
            {QStringLiteral("status_code"), 1008},
            {QStringLiteral("status_msg"), QStringLiteral("insufficient balance")}
        }}
    }).toJson(QJsonDocument::Compact);

    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(&server, &capturedBodies, {{200, businessErrorResponse}});

    VisionApiClient client;
    QString error;
    const auto frame = client.analyzeFrame(
        imagePath,
        QStringLiteral("minimax-error.mov"),
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
        QStringLiteral("test-key"),
        QStringLiteral("MiniMax-M3"),
        5,
        &error);

    QVERIFY(!frame.has_value());
    QCOMPARE(capturedBodies.size(), 1);
    QVERIFY(error.contains(QStringLiteral("1008")));
    QVERIFY(error.contains(QStringLiteral("余额不足")));
    QVERIFY(!error.contains(QStringLiteral("自动修复失败")));
}

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

void VisionApiClientImageTest::testConnection_retriesTransientGatewayFailure()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(
        &server,
        &capturedBodies,
        {
            {502, QByteArray()},
            {200, chatResponseForPayload(QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}})}
        });

    VisionApiClient client;
    QString error;
    const auto result = client.testConnection(
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
        QStringLiteral("test-key"), QStringLiteral("test-model"), 5, &error);

    QVERIFY2(result, qPrintable(error));
    QCOMPARE(capturedBodies.size(), 2);
}

void VisionApiClientImageTest::testConnection_stopsAfterBoundedGatewayRetries()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QVector<QByteArray> capturedBodies;
    installSequentialChatCompletionResponder(
        &server,
        &capturedBodies,
        {
            {502, QByteArray()},
            {502, QByteArray()},
            {200, chatResponseForPayload(QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}})}
        });

    VisionApiClient client;
    QString error;
    const auto result = client.testConnection(
        QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
        QStringLiteral("test-key"), QStringLiteral("test-model"), 5, &error);

    QVERIFY(!result);
    QCOMPARE(capturedBodies.size(), 2);
    QVERIFY(error.contains(QStringLiteral("502")));
    QVERIFY(error.contains(QStringLiteral("已重试 1 次仍失败")));
}

void VisionApiClientImageTest::endpointPolicy_defaultsRemoteToHttpsAndAllowsExplicitHttp()
{
    QCOMPARE(VisionApiClient::normalizedEndpoint(QStringLiteral("api.example.com")),
             QStringLiteral("https://api.example.com/v1/chat/completions"));
    QCOMPARE(VisionApiClient::normalizedEndpoint(QStringLiteral("127.0.0.1:8080/v1")),
             QStringLiteral("http://127.0.0.1:8080/v1/chat/completions"));
    QCOMPARE(VisionApiClient::normalizedEndpoint(QStringLiteral("http://localhost:8080/v1")),
             QStringLiteral("http://localhost:8080/v1/chat/completions"));
    QCOMPARE(VisionApiClient::normalizedEndpoint(QStringLiteral("http://api.example.com/v1")),
             QStringLiteral("http://api.example.com/v1/chat/completions"));
    QVERIFY(VisionApiClient::normalizedEndpoint(QStringLiteral("ftp://api.example.com/v1")).isEmpty());
}

void VisionApiClientImageTest::miniMaxM3_usesNativeEndpointAndFrameConcurrency()
{
    QCOMPARE(VisionApiClient::normalizedEndpoint(QStringLiteral("https://api.minimaxi.com"),
                                                  QStringLiteral("MiniMax-M3")),
             QStringLiteral("https://api.minimaxi.com/v1/text/chatcompletion_v2"));
    QCOMPARE(VisionApiClient::normalizedEndpoint(QStringLiteral("https://api.minimaxi.com/v1/chat/completions"),
                                                  QStringLiteral("minimax-m3")),
             QStringLiteral("https://api.minimaxi.com/v1/text/chatcompletion_v2"));
    QCOMPARE(VisionApiClient::maxConcurrentFrameRequests(QStringLiteral("MiniMax-M3")), 200);
    QCOMPARE(VisionApiClient::maxConcurrentFrameRequests(QStringLiteral("gpt-4.1-mini")), 1);
}

QTEST_MAIN(VisionApiClientImageTest)

#include "VisionApiClientImageTest.moc"
