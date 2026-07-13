#include "infrastructure/network/VisionApiClient.h"

#include "infrastructure/logging/Logger.h"
#include "infrastructure/network/VisionResponseParser.h"

#include <algorithm>
#include <QBuffer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#if defined(CINEVAULT_HAS_BUNDLED_WEBP) && CINEVAULT_HAS_BUNDLED_WEBP
#include <webp/decode.h>
#endif

namespace {
struct HttpResult {
    bool success = false;
    int statusCode = 0;
    QByteArray body;
    QString errorMessage;
};

QString truncateForLog(QString text, int maxLength = 600)
{
    text = text.trimmed();
    if (text.size() <= maxLength) {
        return text;
    }
    return text.left(maxLength) + QStringLiteral("...");
}

QString extractServiceErrorMessage(const QByteArray &body)
{
    const auto plainText = QString::fromUtf8(body).trimmed();
    if (plainText.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return truncateForLog(plainText, 180);
    }

    const auto object = document.object();
    const auto errorValue = object.value(QStringLiteral("error"));
    if (errorValue.isObject()) {
        const auto errorObject = errorValue.toObject();
        const auto message = errorObject.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
        const auto detail = errorObject.value(QStringLiteral("detail")).toString().trimmed();
        if (!detail.isEmpty()) {
            return detail;
        }
    }

    for (const auto &key : {QStringLiteral("message"), QStringLiteral("detail"), QStringLiteral("error_message")}) {
        const auto text = object.value(key).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    return truncateForLog(plainText, 180);
}

QString httpStatusMessage(int statusCode, const QByteArray &body)
{
    QString base;
    if (statusCode == 401) {
        base = QStringLiteral("视觉接口鉴权失败（401）");
    } else if (statusCode == 413) {
        base = QStringLiteral("视觉接口请求内容过大（413）");
    } else if (statusCode == 429) {
        base = QStringLiteral("视觉接口触发限流（429）");
    } else {
        base = QStringLiteral("视觉接口返回异常状态码：%1").arg(statusCode);
    }

    const auto detail = extractServiceErrorMessage(body);
    if (!detail.isEmpty()) {
        base += QStringLiteral("，%1").arg(detail);
    }
    return base;
}

bool shouldRetrySummaryFailure(int statusCode, const QString &errorMessage)
{
    if (statusCode == 400 || statusCode == 413) {
        return true;
    }

    const auto normalizedError = errorMessage.toLower();
    return normalizedError.contains(QStringLiteral("context"))
        || normalizedError.contains(QStringLiteral("token"))
        || normalizedError.contains(QStringLiteral("too large"))
        || normalizedError.contains(QStringLiteral("length"));
}

QVector<FrameAnalysisRecord> sampleFramesForSummary(const QVector<FrameAnalysisRecord> &frames, int maxFrames)
{
    if (maxFrames <= 0 || frames.size() <= maxFrames) {
        return frames;
    }
    if (maxFrames == 1) {
        return QVector<FrameAnalysisRecord>{frames.first()};
    }

    QVector<int> indexes;
    indexes.reserve(maxFrames);
    for (int index = 0; index < maxFrames; ++index) {
        const auto mappedIndex = qRound((static_cast<double>(index) * (frames.size() - 1)) / (maxFrames - 1));
        if (!indexes.contains(mappedIndex)) {
            indexes.append(mappedIndex);
        }
    }
    std::sort(indexes.begin(), indexes.end());

    QVector<FrameAnalysisRecord> sampled;
    sampled.reserve(indexes.size());
    for (const auto index : indexes) {
        sampled.append(frames.at(index));
    }
    return sampled;
}

QString normalizeEndpoint(QString baseUrl)
{
    auto url = baseUrl.trimmed();
    if (url.isEmpty()) {
        return {};
    }
    if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        url = QStringLiteral("http://") + url;
    }
    if (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    if (url.endsWith(QStringLiteral("/chat/completions"))) {
        return url;
    }
    if (url.endsWith(QStringLiteral("/v1"))) {
        return url + QStringLiteral("/chat/completions");
    }

    const QUrl parsedUrl(url);
    if (parsedUrl.path().isEmpty() || parsedUrl.path() == QStringLiteral("/")) {
        return url + QStringLiteral("/v1/chat/completions");
    }
    return url + QStringLiteral("/chat/completions");
}

HttpResult postJson(const QString &endpoint,
                    const QString &apiKey,
                    const QJsonObject &payload,
                    int timeoutSec)
{
    HttpResult result;

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey.trimmed()).toUtf8());
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    auto *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(qMax(5, timeoutSec) * 1000);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        result.errorMessage = QStringLiteral("请求超时");
        reply->deleteLater();
        return result;
    }

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && result.statusCode == 0) {
        result.errorMessage = reply->errorString();
        reply->deleteLater();
        return result;
    }

    if (result.statusCode != 200) {
        result.errorMessage = httpStatusMessage(result.statusCode, result.body);
        Logger::warn(QStringLiteral("视觉接口请求失败：status=%1 endpoint=%2 body=%3")
                         .arg(result.statusCode)
                         .arg(endpoint, truncateForLog(QString::fromUtf8(result.body))));
        reply->deleteLater();
        return result;
    }

    result.success = true;
    reply->deleteLater();
    return result;
}

bool isWebpFile(const QString &imagePath)
{
    return QFileInfo(imagePath).suffix().compare(QStringLiteral("webp"), Qt::CaseInsensitive) == 0;
}

QImage loadImageWithQt(const QString &imagePath, QString *errorMessage)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    auto image = reader.read();
    if (image.isNull() && errorMessage) {
        const auto readerError = reader.errorString().trimmed();
        *errorMessage = readerError.isEmpty() ? QStringLiteral("图片解码失败") : readerError;
    }
    return image;
}

#if defined(CINEVAULT_HAS_BUNDLED_WEBP) && CINEVAULT_HAS_BUNDLED_WEBP
QImage loadImageWithBundledWebpDecoder(const QString &imagePath, QString *errorMessage)
{
    if (!isWebpFile(imagePath)) {
        return {};
    }

    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return {};
    }

    const auto encodedBytes = file.readAll();
    if (encodedBytes.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文件内容为空");
        }
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
        if (errorMessage) {
            *errorMessage = QStringLiteral("WebP 文件头无效");
        }
        return {};
    }

    QImage image(width, height, QImage::Format_RGBA8888);
    if (image.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法分配 WebP 解码缓冲区");
        }
        return {};
    }

    const auto *decoded = WebPDecodeRGBAInto(
        reinterpret_cast<const uint8_t *>(encodedBytes.constData()),
        static_cast<size_t>(encodedBytes.size()),
        image.bits(),
        static_cast<size_t>(image.sizeInBytes()),
        image.bytesPerLine());
    if (!decoded) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("WebP 像素解码失败");
        }
        return {};
    }

    return image;
}
#endif

QImage loadImageForVision(const QString &imagePath, QString *errorMessage)
{
    QString qtError;
    auto image = loadImageWithQt(imagePath, &qtError);
    if (!image.isNull()) {
        return image;
    }

#if defined(CINEVAULT_HAS_BUNDLED_WEBP) && CINEVAULT_HAS_BUNDLED_WEBP
    if (isWebpFile(imagePath)) {
        QString webpError;
        image = loadImageWithBundledWebpDecoder(imagePath, &webpError);
        if (!image.isNull()) {
            return image;
        }
        if (errorMessage) {
            *errorMessage = webpError.trimmed().isEmpty()
                ? qtError
                : QStringLiteral("%1；WebP 兜底解码失败：%2").arg(qtError, webpError);
        }
        return {};
    }
#endif

    if (errorMessage) {
        *errorMessage = qtError;
    }
    return {};
}

std::optional<QString> imageAsJpegDataUrl(const QString &imagePath, QString *errorMessage)
{
    QString decodeError;
    auto image = loadImageForVision(imagePath, &decodeError);
    if (image.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法将图片转换为 JPG：%1，%2").arg(imagePath, decodeError);
        }
        return std::nullopt;
    }
    if (image.hasAlphaChannel()) {
        image = image.convertToFormat(QImage::Format_RGB888);
    }

    QByteArray jpegBytes;
    QBuffer buffer(&jpegBytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPG", 90)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("图片转 JPG 失败：%1").arg(imagePath);
        }
        return std::nullopt;
    }
    return QStringLiteral("data:image/jpeg;base64,%1")
        .arg(QString::fromUtf8(jpegBytes.toBase64()));
}

QJsonObject stringArraySchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}
    };
}

QJsonObject genericJsonSchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), true}
    };
}

QJsonObject statusSchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("status"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("status")}},
        {QStringLiteral("additionalProperties"), false}
    };
}

QJsonObject frameAnalysisSchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("caption"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             {QStringLiteral("tags"), stringArraySchema()},
             {QStringLiteral("objects"), stringArraySchema()},
             {QStringLiteral("actions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             {QStringLiteral("setting"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}
         }},
        {QStringLiteral("required"),
         QJsonArray{
             QStringLiteral("caption"),
             QStringLiteral("tags"),
             QStringLiteral("objects"),
             QStringLiteral("actions"),
             QStringLiteral("setting")
         }},
        {QStringLiteral("additionalProperties"), false}
    };
}

QJsonObject videoSummarySchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("summary"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             {QStringLiteral("keywords"), stringArraySchema()},
             {QStringLiteral("scenes"), stringArraySchema()}
         }},
        {QStringLiteral("required"),
         QJsonArray{QStringLiteral("summary"), QStringLiteral("keywords"), QStringLiteral("scenes")}},
        {QStringLiteral("additionalProperties"), false}
    };
}

QJsonObject dimensionAnalysisSchema()
{
    const QJsonObject dimensionItem{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             {QStringLiteral("detail"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("name"), QStringLiteral("detail")}},
        {QStringLiteral("additionalProperties"), false}
    };

    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("dimensions"),
              QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("array")},
                  {QStringLiteral("items"), dimensionItem}
              }}
         }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("dimensions")}},
        {QStringLiteral("additionalProperties"), false}
    };
}

QJsonObject jsonSchemaResponseFormat(const QString &name, const QJsonObject &schema)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("json_schema")},
        {QStringLiteral("json_schema"),
         QJsonObject{
             {QStringLiteral("name"), name},
             {QStringLiteral("strict"), false},
             {QStringLiteral("schema"), schema}
         }}
    };
}

QJsonObject textResponseFormat()
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}};
}

QJsonObject makeChatPayload(const QString &model,
                            const QJsonArray &content,
                            int maxTokens,
                            const QString &schemaName,
                            const QJsonObject &schema)
{
    return QJsonObject{
        {QStringLiteral("model"), model},
        {QStringLiteral("max_tokens"), maxTokens},
        {QStringLiteral("response_format"), jsonSchemaResponseFormat(schemaName, schema)},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("content"), content}
            }
        }}
    };
}

bool isResponseFormatRejected(const HttpResult &result)
{
    if (result.statusCode != 400) {
        return false;
    }
    const auto bodyText = QString::fromUtf8(result.body);
    return bodyText.contains(QStringLiteral("response_format"), Qt::CaseInsensitive);
}

HttpResult postChatPayload(const QString &endpoint,
                           const QString &apiKey,
                           QJsonObject payload,
                           int timeoutSec)
{
    auto result = postJson(endpoint, apiKey, payload, timeoutSec);
    if (!result.success && isResponseFormatRejected(result)) {
        payload.insert(QStringLiteral("response_format"), textResponseFormat());
        result = postJson(endpoint, apiKey, payload, timeoutSec);
    }
    return result;
}

std::optional<QJsonObject> repairAssistantPayload(const QByteArray &responseBody,
                                                  const QString &endpoint,
                                                  const QString &apiKey,
                                                  const QString &model,
                                                  int timeoutSec,
                                                  const QString &schema,
                                                  const QString &failureReason,
                                                  int maxTokens,
                                                  QString *errorMessage)
{
    QString contentError;
    const auto originalContent = VisionResponseParser::extractAssistantContent(responseBody, &contentError);
    if (!originalContent.has_value()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1；自动修复失败：无法读取原始返回内容（%2）")
                                .arg(failureReason, contentError);
        }
        return std::nullopt;
    }

    auto boundedContent = originalContent->trimmed();
    if (boundedContent.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1；自动修复失败：原始返回内容为空").arg(failureReason);
        }
        return std::nullopt;
    }
    if (boundedContent.size() > 12000) {
        boundedContent = boundedContent.left(12000);
    }

    const QJsonArray content = {
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"),
             QStringLiteral("你是视觉解析结果格式修复器。请把原始返回内容转换为严格 JSON。"
                            "只能使用原始内容中已经出现的信息，不要新增事实，不要解释，不要 Markdown。"
                            "目标 JSON 结构为：%1\n\n"
                            "上一次失败原因：%2\n\n"
                            "原始返回内容：\n%3")
                 .arg(schema, failureReason, boundedContent)}
        }
    };

    const auto result = postChatPayload(endpoint,
                                        apiKey,
                                        makeChatPayload(model,
                                                        content,
                                                        maxTokens,
                                                        QStringLiteral("vision_repair"),
                                                        genericJsonSchema()),
                                        timeoutSec);
    if (!result.success) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1；自动修复失败：%2").arg(failureReason, result.errorMessage);
        }
        return std::nullopt;
    }

    QString repairParseError;
    auto repairedPayload = VisionResponseParser::parseAssistantJson(result.body, &repairParseError);
    if (!repairedPayload.has_value()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1；自动修复失败：%2").arg(failureReason, repairParseError);
        }
        return std::nullopt;
    }
    return repairedPayload;
}

QString combineFallbackError(const QString &existingError, const QString &fallbackError)
{
    if (existingError.trimmed().isEmpty()) {
        return QStringLiteral("纯文本兜底失败：%1").arg(fallbackError);
    }
    return QStringLiteral("%1；纯文本兜底失败：%2").arg(existingError, fallbackError);
}

std::optional<VisionFrameAnalysis> fallbackFrameAnalysisFromResponse(const QByteArray &responseBody,
                                                                    QString *errorMessage)
{
    const auto existingError = errorMessage ? *errorMessage : QString();

    QString contentError;
    const auto content = VisionResponseParser::extractAssistantContent(responseBody, &contentError);
    if (!content.has_value()) {
        if (errorMessage) {
            *errorMessage = combineFallbackError(existingError, contentError);
        }
        return std::nullopt;
    }

    QString fallbackError;
    auto analysis = VisionResponseParser::fallbackFrameAnalysisFromContent(*content, &fallbackError);
    if (!analysis.has_value()) {
        if (errorMessage) {
            *errorMessage = combineFallbackError(existingError, fallbackError);
        }
        return std::nullopt;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return analysis;
}

std::optional<VisionVideoSummary> fallbackVideoSummaryFromResponse(const QByteArray &responseBody,
                                                                  QString *errorMessage)
{
    const auto existingError = errorMessage ? *errorMessage : QString();

    QString contentError;
    const auto content = VisionResponseParser::extractAssistantContent(responseBody, &contentError);
    if (!content.has_value()) {
        if (errorMessage) {
            *errorMessage = combineFallbackError(existingError, contentError);
        }
        return std::nullopt;
    }

    QString fallbackError;
    auto summary = VisionResponseParser::fallbackVideoSummaryFromContent(*content, &fallbackError);
    if (!summary.has_value()) {
        if (errorMessage) {
            *errorMessage = combineFallbackError(existingError, fallbackError);
        }
        return std::nullopt;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return summary;
}

QString fallbackFileName(const QString &fileName, const QString &path)
{
    const auto normalizedName = fileName.trimmed();
    if (!normalizedName.isEmpty()) {
        return normalizedName;
    }
    return QFileInfo(path).fileName();
}

QStringList normalizedDimensionNames(const QStringList &dimensions)
{
    QStringList normalized;
    for (const auto &dimension : dimensions) {
        const auto name = dimension.simplified();
        if (name.isEmpty()) {
            continue;
        }
        bool exists = false;
        for (const auto &existing : normalized) {
            if (existing.compare(name, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            normalized.append(name);
        }
    }
    return normalized;
}

QString jsonValueToText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().simplified();
    }
    if (value.isArray()) {
        QStringList parts;
        const auto array = value.toArray();
        for (const auto &item : array) {
            const auto text = jsonValueToText(item);
            if (!text.isEmpty()) {
                parts.append(text);
            }
        }
        return parts.join(QStringLiteral("；"));
    }
    if (value.isObject()) {
        const auto object = value.toObject();
        for (const auto &key : {
                 QStringLiteral("detail"),
                 QStringLiteral("analysis"),
                 QStringLiteral("summary"),
                 QStringLiteral("description"),
                 QStringLiteral("text"),
                 QStringLiteral("result")}) {
            const auto text = jsonValueToText(object.value(key));
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return {};
}

QJsonArray dimensionNameArray(const QStringList &dimensions)
{
    QJsonArray array;
    for (const auto &dimension : dimensions) {
        array.append(dimension);
    }
    return array;
}

std::optional<QVector<MaterialDimensionAnalysis>> normalizeDimensionAnalyses(const QJsonObject &payload,
                                                                            const QStringList &requestedDimensions,
                                                                            QString *errorMessage)
{
    QVector<MaterialDimensionAnalysis> analyses;

    auto appendAnalysis = [&](QString name, QString detail) {
        name = name.simplified();
        detail = detail.simplified();
        if (name.isEmpty() || detail.isEmpty()) {
            return;
        }
        for (const auto &existing : analyses) {
            if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        MaterialDimensionAnalysis analysis;
        analysis.name = name;
        analysis.detail = detail;
        analyses.append(analysis);
    };

    QJsonArray array;
    for (const auto &key : {
             QStringLiteral("dimensions"),
             QStringLiteral("results"),
             QStringLiteral("items"),
             QStringLiteral("analyses")}) {
        const auto value = payload.value(key);
        if (value.isArray()) {
            array = value.toArray();
            break;
        }
    }

    if (!array.isEmpty()) {
        for (int index = 0; index < array.size(); ++index) {
            const auto value = array.at(index);
            if (value.isObject()) {
                const auto object = value.toObject();
                auto name = object.value(QStringLiteral("name")).toString().trimmed();
                if (name.isEmpty()) {
                    name = object.value(QStringLiteral("dimension")).toString().trimmed();
                }
                if (name.isEmpty()) {
                    name = object.value(QStringLiteral("title")).toString().trimmed();
                }
                if (name.isEmpty() && index < requestedDimensions.size()) {
                    name = requestedDimensions.at(index);
                }
                appendAnalysis(name, jsonValueToText(value));
            } else if (index < requestedDimensions.size()) {
                appendAnalysis(requestedDimensions.at(index), jsonValueToText(value));
            }
        }
    }

    if (analyses.isEmpty()) {
        for (const auto &dimension : requestedDimensions) {
            appendAnalysis(dimension, jsonValueToText(payload.value(dimension)));
        }
    }

    if (analyses.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("视觉接口返回多维度解析字段为空");
        }
        return std::nullopt;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return analyses;
}
}

bool VisionApiClient::testConnection(const QString &baseUrl,
                                     const QString &apiKey,
                                     const QString &model,
                                     int timeoutSec,
                                     QString *errorMessage) const
{
    const auto endpoint = normalizeEndpoint(baseUrl);
    if (endpoint.trimmed().isEmpty() || apiKey.trimmed().isEmpty() || model.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请先完整填写 Base URL、API Key 和模型名");
        }
        return false;
    }

    const QJsonArray content = {
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("请只返回 JSON：{\"status\":\"ok\"}")}}
    };
    const auto result = postChatPayload(endpoint,
                                        apiKey,
                                        makeChatPayload(model,
                                                        content,
                                                        32,
                                                        QStringLiteral("vision_status"),
                                                        statusSchema()),
                                        timeoutSec);
    if (!result.success) {
        if (errorMessage) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    auto payload = VisionResponseParser::parseAssistantJson(result.body, errorMessage);
    return payload.has_value() && payload->value(QStringLiteral("status")).toString().trimmed() == QStringLiteral("ok");
}

std::optional<VisionFrameAnalysis> VisionApiClient::analyzeFrame(const QString &imagePath,
                                                                 const QString &sourceFileName,
                                                                 const QString &baseUrl,
                                                                 const QString &apiKey,
                                                                 const QString &model,
                                                                 int timeoutSec,
                                                                 QString *errorMessage,
                                                                 int *httpStatusCode) const
{
    auto imageDataUrl = imageAsJpegDataUrl(imagePath, errorMessage);
    if (!imageDataUrl.has_value()) {
        return std::nullopt;
    }

    const auto endpoint = normalizeEndpoint(baseUrl);
    const auto fileName = fallbackFileName(sourceFileName, imagePath);
    const QJsonArray content = {
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"),
                     QStringLiteral("这是素材文件《%1》的抽帧图片。请结合文件名线索分析这张视频帧，只返回 JSON，格式为 "
                                    "{\"caption\":\"\",\"tags\":[],\"objects\":[],\"actions\":\"\",\"setting\":\"\"}。"
                                    "caption 用一句中文概括画面，tags/objects 为中文关键词数组。"
                                    "如果文件名包含品牌、日期、地点、项目名或版本信息，可作为辅助线索写入可确认的描述中。")
                         .arg(fileName)}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"),
                     QJsonObject{{QStringLiteral("url"), *imageDataUrl}}}}
    };

    const auto result = postChatPayload(endpoint,
                                        apiKey,
                                        makeChatPayload(model,
                                                        content,
                                                        300,
                                                        QStringLiteral("vision_frame_analysis"),
                                                        frameAnalysisSchema()),
                                        timeoutSec);
    if (httpStatusCode) {
        *httpStatusCode = result.statusCode;
    }
    if (!result.success) {
        if (errorMessage) {
            *errorMessage = result.errorMessage;
        }
        return std::nullopt;
    }

    const auto schema = QStringLiteral("{\"caption\":\"\",\"tags\":[],\"objects\":[],\"actions\":\"\",\"setting\":\"\"}");
    QString parseError;
    auto payload = VisionResponseParser::parseAssistantJson(result.body, &parseError);
    auto usedRepair = false;
    if (!payload.has_value()) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         parseError,
                                         350,
                                         errorMessage);
        usedRepair = true;
        if (!payload.has_value()) {
            return fallbackFrameAnalysisFromResponse(result.body, errorMessage);
        }
    }

    QString normalizeError;
    auto analysis = VisionResponseParser::normalizeFrameAnalysis(*payload, &normalizeError);
    if (!analysis.has_value() && !usedRepair) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         normalizeError,
                                         350,
                                         errorMessage);
        usedRepair = true;
        if (payload.has_value()) {
            analysis = VisionResponseParser::normalizeFrameAnalysis(*payload, &normalizeError);
        } else {
            return fallbackFrameAnalysisFromResponse(result.body, errorMessage);
        }
    }
    if (!analysis.has_value()) {
        auto fallback = fallbackFrameAnalysisFromResponse(result.body, errorMessage);
        if (fallback.has_value()) {
            return fallback;
        }
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = normalizeError;
        }
    }
    return analysis;
}

std::optional<QVector<MaterialDimensionAnalysis>> VisionApiClient::analyzeFrameDimensions(const QString &imagePath,
                                                                                          const QString &sourceFileName,
                                                                                          const QString &frameContext,
                                                                                          const QStringList &dimensions,
                                                                                          const QString &baseUrl,
                                                                                          const QString &apiKey,
                                                                                          const QString &model,
                                                                                          int timeoutSec,
                                                                                          QString *errorMessage,
                                                                                          int *httpStatusCode) const
{
    const auto requestedDimensions = normalizedDimensionNames(dimensions);
    if (requestedDimensions.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请至少添加一个解析维度");
        }
        return std::nullopt;
    }

    auto imageDataUrl = imageAsJpegDataUrl(imagePath, errorMessage);
    if (!imageDataUrl.has_value()) {
        return std::nullopt;
    }

    const auto endpoint = normalizeEndpoint(baseUrl);
    const auto displayName = fallbackFileName(sourceFileName, imagePath);
    const auto schema = QStringLiteral("{\"dimensions\":[{\"name\":\"\",\"detail\":\"\"}]}");
    const auto maxTokens = qBound(500, requestedDimensions.size() * 220 + 260, 1200);
    const QJsonArray content = {
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"),
                     QStringLiteral("这是素材文件《%1》的一张视频帧。请只基于当前这一帧图片和这帧已有基础描述，"
                                    "逐项补充用户指定的多维度分析。不要引用其他帧，不要汇总整段视频。"
                                    "每个维度的 detail 用 1-2 句中文记录此帧可确认的细节、线索或不可见结论；"
                                    "如果当前帧没有对应线索，也要明确写出“此帧未见明确线索”。"
                                    "只返回 JSON，不要加入 Markdown。目标 JSON 格式为 %2。\n\n"
                                    "需要分析的维度：%3\n\n"
                                    "当前帧基础描述：\n%4")
                         .arg(displayName,
                              schema,
                              QString::fromUtf8(QJsonDocument(dimensionNameArray(requestedDimensions)).toJson(QJsonDocument::Compact)),
                              frameContext.trimmed().left(1600))}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"),
                     QJsonObject{{QStringLiteral("url"), *imageDataUrl}}}}
    };

    const auto result = postChatPayload(endpoint,
                                        apiKey,
                                        makeChatPayload(model,
                                                        content,
                                                        maxTokens,
                                                        QStringLiteral("vision_dimension_analysis"),
                                                        dimensionAnalysisSchema()),
                                        timeoutSec);
    if (httpStatusCode) {
        *httpStatusCode = result.statusCode;
    }
    if (!result.success) {
        if (errorMessage) {
            *errorMessage = result.errorMessage;
        }
        return std::nullopt;
    }

    QString parseError;
    auto payload = VisionResponseParser::parseAssistantJson(result.body, &parseError);
    auto usedRepair = false;
    if (!payload.has_value()) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         parseError,
                                         maxTokens,
                                         errorMessage);
        usedRepair = true;
        if (!payload.has_value()) {
            return std::nullopt;
        }
    }

    QString normalizeError;
    auto analyses = normalizeDimensionAnalyses(*payload, requestedDimensions, &normalizeError);
    if (!analyses.has_value() && !usedRepair) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         normalizeError,
                                         maxTokens,
                                         errorMessage);
        if (payload.has_value()) {
            analyses = normalizeDimensionAnalyses(*payload, requestedDimensions, &normalizeError);
        }
    }
    if (!analyses.has_value() && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = normalizeError;
    }
    return analyses;
}

std::optional<VisionVideoSummary> VisionApiClient::analyzeImage(const QString &imagePath,
                                                                const QString &fileName,
                                                                const QString &baseUrl,
                                                                const QString &apiKey,
                                                                const QString &model,
                                                                int timeoutSec,
                                                                QString *errorMessage,
                                                                int *httpStatusCode) const
{
    auto imageDataUrl = imageAsJpegDataUrl(imagePath, errorMessage);
    if (!imageDataUrl.has_value()) {
        return std::nullopt;
    }

    const auto endpoint = normalizeEndpoint(baseUrl);
    const auto displayName = fallbackFileName(fileName, imagePath);
    const QJsonArray content = {
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"),
                     QStringLiteral("请结合文件名《%1》分析这张图片素材，只返回 JSON，格式为 "
                                    "{\"summary\":\"\",\"keywords\":[],\"scenes\":[]}。"
                                    "summary 用 3-5 句中文说明主要主体、场景环境、可见物体、动作/状态、风格和可用于检索的画面特征。"
                                    "文件名中的品牌、活动、日期、地点、项目名、版本号等信息可作为辅助线索，但不要编造画面中不可确认的事实。"
                                    "keywords 返回 8-16 个中文搜索关键词，scenes 返回场景/地点/环境类中文关键词数组。"
                                    "不要加入 Markdown。")
                         .arg(displayName)}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"),
                     QJsonObject{{QStringLiteral("url"), *imageDataUrl}}}}
    };

    const auto result = postChatPayload(endpoint,
                                        apiKey,
                                        makeChatPayload(model,
                                                        content,
                                                        700,
                                                        QStringLiteral("vision_video_summary"),
                                                        videoSummarySchema()),
                                        timeoutSec);
    if (httpStatusCode) {
        *httpStatusCode = result.statusCode;
    }
    if (!result.success) {
        if (errorMessage) {
            *errorMessage = result.errorMessage;
        }
        return std::nullopt;
    }

    const auto schema = QStringLiteral("{\"summary\":\"\",\"keywords\":[],\"scenes\":[]}");
    QString parseError;
    auto payload = VisionResponseParser::parseAssistantJson(result.body, &parseError);
    auto usedRepair = false;
    if (!payload.has_value()) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         parseError,
                                         700,
                                         errorMessage);
        usedRepair = true;
        if (!payload.has_value()) {
            return fallbackVideoSummaryFromResponse(result.body, errorMessage);
        }
    }

    QString normalizeError;
    auto summary = VisionResponseParser::normalizeVideoSummary(*payload, &normalizeError);
    if (!summary.has_value() && !usedRepair) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         normalizeError,
                                         700,
                                         errorMessage);
        usedRepair = true;
        if (payload.has_value()) {
            summary = VisionResponseParser::normalizeVideoSummary(*payload, &normalizeError);
        } else {
            return fallbackVideoSummaryFromResponse(result.body, errorMessage);
        }
    }
    if (!summary.has_value()) {
        auto fallback = fallbackVideoSummaryFromResponse(result.body, errorMessage);
        if (fallback.has_value()) {
            return fallback;
        }
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = normalizeError;
        }
    }
    return summary;
}

std::optional<VisionVideoSummary> VisionApiClient::summarizeText(const QString &fileName,
                                                                 const QString &text,
                                                                 const QString &baseUrl,
                                                                 const QString &apiKey,
                                                                 const QString &model,
                                                                 int timeoutSec,
                                                                 QString *errorMessage,
                                                                 int *httpStatusCode) const
{
    const auto boundedText = text.trimmed().left(64000);
    if (boundedText.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("没有可用于摘要的文本内容");
        }
        return std::nullopt;
    }

    const auto endpoint = normalizeEndpoint(baseUrl);
    const QJsonArray content = {
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"),
                     QStringLiteral("下面是素材文件《%1》的文本内容，请归纳为中文 JSON，格式为 "
                                    "{\"summary\":\"\",\"keywords\":[],\"scenes\":[]}。"
                                    "summary 用 3-6 句概括内容主题、关键信息、可能用途和可检索要点；"
                                    "keywords 返回 8-16 个中文搜索关键词；scenes 可返回文档中出现的场景、地点、项目或主题分类，没有则返回空数组。"
                                    "只返回 JSON，不要加入 Markdown。\n\n%2")
                         .arg(fileName, boundedText)}}
    };

    const auto result = postChatPayload(endpoint,
                                        apiKey,
                                        makeChatPayload(model,
                                                        content,
                                                        800,
                                                        QStringLiteral("vision_video_summary"),
                                                        videoSummarySchema()),
                                        timeoutSec);
    if (httpStatusCode) {
        *httpStatusCode = result.statusCode;
    }
    if (!result.success) {
        if (errorMessage) {
            *errorMessage = result.errorMessage;
        }
        return std::nullopt;
    }

    const auto schema = QStringLiteral("{\"summary\":\"\",\"keywords\":[],\"scenes\":[]}");
    QString parseError;
    auto payload = VisionResponseParser::parseAssistantJson(result.body, &parseError);
    auto usedRepair = false;
    if (!payload.has_value()) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         parseError,
                                         800,
                                         errorMessage);
        usedRepair = true;
        if (!payload.has_value()) {
            return fallbackVideoSummaryFromResponse(result.body, errorMessage);
        }
    }

    QString normalizeError;
    auto summary = VisionResponseParser::normalizeVideoSummary(*payload, &normalizeError);
    if (!summary.has_value() && !usedRepair) {
        payload = repairAssistantPayload(result.body,
                                         endpoint,
                                         apiKey,
                                         model,
                                         timeoutSec,
                                         schema,
                                         normalizeError,
                                         800,
                                         errorMessage);
        usedRepair = true;
        if (payload.has_value()) {
            summary = VisionResponseParser::normalizeVideoSummary(*payload, &normalizeError);
        } else {
            return fallbackVideoSummaryFromResponse(result.body, errorMessage);
        }
    }
    if (!summary.has_value()) {
        auto fallback = fallbackVideoSummaryFromResponse(result.body, errorMessage);
        if (fallback.has_value()) {
            return fallback;
        }
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = normalizeError;
        }
    }
    return summary;
}

std::optional<VisionVideoSummary> VisionApiClient::summarizeVideo(const QString &fileName,
                                                                  const QVector<FrameAnalysisRecord> &frames,
                                                                  const QString &baseUrl,
                                                                  const QString &apiKey,
                                                                  const QString &model,
                                                                  int timeoutSec,
                                                                  QString *errorMessage,
                                                                  int *attemptCount,
                                                                  int *httpStatusCode) const
{
    const auto endpoint = normalizeEndpoint(baseUrl);
    if (attemptCount) {
        *attemptCount = 0;
    }
    if (httpStatusCode) {
        *httpStatusCode = 0;
    }

    auto attemptSummary = [&](const QVector<FrameAnalysisRecord> &sourceFrames,
                              int maxTokens,
                              QString *attemptError,
                              int *attemptStatusCode) -> std::optional<VisionVideoSummary> {
        QStringList frameLines;
        for (const auto &frame : sourceFrames) {
            QStringList parts;
            if (!frame.caption.trimmed().isEmpty()) {
                parts.append(QStringLiteral("描述：%1").arg(frame.caption.trimmed()));
            }
            if (!frame.tags.isEmpty()) {
                parts.append(QStringLiteral("标签：%1").arg(frame.tags.join(QStringLiteral("、"))));
            }
            if (!frame.objects.isEmpty()) {
                parts.append(QStringLiteral("对象：%1").arg(frame.objects.join(QStringLiteral("、"))));
            }
            if (!frame.actions.trimmed().isEmpty()) {
                parts.append(QStringLiteral("动作：%1").arg(frame.actions.trimmed()));
            }
            if (!frame.setting.trimmed().isEmpty()) {
                parts.append(QStringLiteral("场景：%1").arg(frame.setting.trimmed()));
            }
            if (!parts.isEmpty()) {
                frameLines.append(QStringLiteral("第 %1 帧：%2").arg(frame.frameNumber).arg(parts.join(QStringLiteral("；"))));
            }
        }

        if (frameLines.isEmpty()) {
            if (attemptError) {
                *attemptError = QStringLiteral("没有可用于汇总的视频帧描述");
            }
            if (attemptStatusCode) {
                *attemptStatusCode = 0;
            }
            return std::nullopt;
        }

        const QJsonArray content = {
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"),
                         QStringLiteral("下面是视频《%1》的抽帧解析结果，请汇总成中文 JSON，格式为 "
                                        "{\"summary\":\"\",\"keywords\":[],\"scenes\":[]}。"
                                        "summary 需要比普通标题摘要更详细，用 4-6 句说明视频的主要主体、场景环境、动作变化、"
                                        "可见物体、情绪/氛围和镜头内容变化；如果抽帧信息有限，也要基于已有帧明确说明可确认内容。"
                                        "keywords 返回 8-16 个适合搜索的中文关键词，scenes 返回场景/地点/环境类中文关键词数组。"
                                        "只返回 JSON，不要加入 Markdown。\n\n%2")
                             .arg(fileName, frameLines.join(QStringLiteral("\n"))) }}
        };

        const auto result = postChatPayload(endpoint,
                                            apiKey,
                                            makeChatPayload(model,
                                                            content,
                                                            maxTokens,
                                                            QStringLiteral("vision_video_summary"),
                                                            videoSummarySchema()),
                                            timeoutSec);
        if (attemptStatusCode) {
            *attemptStatusCode = result.statusCode;
        }
        if (!result.success) {
            if (attemptError) {
                *attemptError = result.errorMessage;
            }
            return std::nullopt;
        }

        const auto schema = QStringLiteral("{\"summary\":\"\",\"keywords\":[],\"scenes\":[]}");
        QString parseError;
        auto payload = VisionResponseParser::parseAssistantJson(result.body, &parseError);
        auto usedRepair = false;
        if (!payload.has_value()) {
            payload = repairAssistantPayload(result.body,
                                             endpoint,
                                             apiKey,
                                             model,
                                             timeoutSec,
                                             schema,
                                             parseError,
                                             maxTokens,
                                             attemptError);
            usedRepair = true;
            if (!payload.has_value()) {
                return fallbackVideoSummaryFromResponse(result.body, attemptError);
            }
        }

        QString normalizeError;
        auto summary = VisionResponseParser::normalizeVideoSummary(*payload, &normalizeError);
        if (!summary.has_value() && !usedRepair) {
            payload = repairAssistantPayload(result.body,
                                             endpoint,
                                             apiKey,
                                             model,
                                             timeoutSec,
                                             schema,
                                             normalizeError,
                                             maxTokens,
                                             attemptError);
            usedRepair = true;
            if (payload.has_value()) {
                summary = VisionResponseParser::normalizeVideoSummary(*payload, &normalizeError);
            } else {
                return fallbackVideoSummaryFromResponse(result.body, attemptError);
            }
        }
        if (!summary.has_value()) {
            auto fallback = fallbackVideoSummaryFromResponse(result.body, attemptError);
            if (fallback.has_value()) {
                return fallback;
            }
            if (attemptError && attemptError->isEmpty()) {
                *attemptError = normalizeError;
            }
        }
        return summary;
    };

    QVector<QPair<int, int>> plans;
    auto appendPlan = [&](int maxFrames, int maxTokens) {
        for (const auto &plan : plans) {
            if (plan.first == maxFrames && plan.second == maxTokens) {
                return;
            }
        }
        plans.append({maxFrames, maxTokens});
    };

    appendPlan(0, 900);
    appendPlan(qMin(18, frames.size()), 700);
    appendPlan(qMin(12, frames.size()), 550);

    QString lastError = QStringLiteral("视频内容汇总失败");
    int lastStatusCode = 0;
    int usedAttempts = 0;
    for (int index = 0; index < plans.size(); ++index) {
        const auto &plan = plans.at(index);
        ++usedAttempts;
        const auto sampledFrames = sampleFramesForSummary(frames, plan.first);
        const auto summary = attemptSummary(sampledFrames, plan.second, &lastError, &lastStatusCode);
        if (summary.has_value()) {
            if (attemptCount) {
                *attemptCount = usedAttempts;
            }
            if (httpStatusCode) {
                *httpStatusCode = lastStatusCode;
            }
            return summary;
        }

        if (index >= plans.size() - 1 || !shouldRetrySummaryFailure(lastStatusCode, lastError)) {
            break;
        }
    }

    if (attemptCount) {
        *attemptCount = usedAttempts;
    }
    if (httpStatusCode) {
        *httpStatusCode = lastStatusCode;
    }
    if (errorMessage) {
        *errorMessage = lastError;
    }
    return std::nullopt;
}

std::optional<QVector<MaterialDimensionAnalysis>> VisionApiClient::analyzeDimensions(const QString &fileName,
                                                                                     const QString &baseContext,
                                                                                     const QStringList &dimensions,
                                                                                     const QString &baseUrl,
                                                                                     const QString &apiKey,
                                                                                     const QString &model,
                                                                                     int timeoutSec,
                                                                                     QString *errorMessage,
                                                                                     int *httpStatusCode) const
{
    const auto requestedDimensions = normalizedDimensionNames(dimensions);
    if (requestedDimensions.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("请至少添加一个解析维度");
        }
        return std::nullopt;
    }

    const auto normalizedContext = baseContext.trimmed();
    if (normalizedContext.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("没有可用于多维度解析的基础解析内容");
        }
        return std::nullopt;
    }

    const auto endpoint = normalizeEndpoint(baseUrl);
    const auto schema = QStringLiteral("{\"dimensions\":[{\"name\":\"\",\"detail\":\"\"}]}");

    auto attemptDimensions = [&](int maxContextChars,
                                 int maxTokens,
                                 QString *attemptError,
                                 int *attemptStatusCode) -> std::optional<QVector<MaterialDimensionAnalysis>> {
        if (attemptError) {
            attemptError->clear();
        }
        if (attemptStatusCode) {
            *attemptStatusCode = 0;
        }

        const auto boundedContext = normalizedContext.left(maxContextChars).trimmed();
        if (boundedContext.isEmpty()) {
            if (attemptError) {
                *attemptError = QStringLiteral("没有可用于多维度解析的基础解析内容");
            }
            return std::nullopt;
        }

        const QJsonArray content = {
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                        {QStringLiteral("text"),
                         QStringLiteral("下面是素材文件《%1》的基础解析结果。请只基于这些已解析信息，补充用户指定的多维度分析。"
                                        "不要重复改写已有摘要、关键词、场景；不要重新解析已存在维度；不要编造基础信息中无法确认的事实。"
                                        "每个维度的 detail 用 2-4 句中文说明可确认的观察、用途或判断依据。"
                                        "只返回 JSON，不要加入 Markdown。目标 JSON 格式为 %2。\n\n"
                                        "需要补充的维度：%3\n\n"
                                        "基础解析结果：\n%4")
                             .arg(fileName.trimmed().isEmpty() ? QStringLiteral("未命名素材") : fileName.trimmed(),
                                  schema,
                                  QString::fromUtf8(QJsonDocument(dimensionNameArray(requestedDimensions)).toJson(QJsonDocument::Compact)),
                                  boundedContext)}}
        };

        const auto result = postChatPayload(endpoint,
                                            apiKey,
                                            makeChatPayload(model,
                                                            content,
                                                            maxTokens,
                                                            QStringLiteral("vision_dimension_analysis"),
                                                            dimensionAnalysisSchema()),
                                            timeoutSec);
        if (attemptStatusCode) {
            *attemptStatusCode = result.statusCode;
        }
        if (!result.success) {
            if (attemptError) {
                *attemptError = result.errorMessage;
            }
            return std::nullopt;
        }

        QString parseError;
        auto payload = VisionResponseParser::parseAssistantJson(result.body, &parseError);
        auto usedRepair = false;
        if (!payload.has_value()) {
            payload = repairAssistantPayload(result.body,
                                             endpoint,
                                             apiKey,
                                             model,
                                             timeoutSec,
                                             schema,
                                             parseError,
                                             maxTokens,
                                             attemptError);
            usedRepair = true;
            if (!payload.has_value()) {
                return std::nullopt;
            }
        }

        QString normalizeError;
        auto analyses = normalizeDimensionAnalyses(*payload, requestedDimensions, &normalizeError);
        if (!analyses.has_value() && !usedRepair) {
            payload = repairAssistantPayload(result.body,
                                             endpoint,
                                             apiKey,
                                             model,
                                             timeoutSec,
                                             schema,
                                             normalizeError,
                                             maxTokens,
                                             attemptError);
            if (payload.has_value()) {
                analyses = normalizeDimensionAnalyses(*payload, requestedDimensions, &normalizeError);
            }
        }
        if (!analyses.has_value() && attemptError && attemptError->isEmpty()) {
            *attemptError = normalizeError;
        }
        return analyses;
    };

    QVector<QPair<int, int>> plans;
    auto appendPlan = [&](int maxContextChars, int maxTokens) {
        const auto boundedChars = qBound(1, maxContextChars, normalizedContext.size());
        for (const auto &plan : plans) {
            if (plan.first == boundedChars && plan.second == maxTokens) {
                return;
            }
        }
        plans.append({boundedChars, maxTokens});
    };

    appendPlan(12000, 1000);
    appendPlan(8000, 800);
    appendPlan(4000, 650);
    appendPlan(2000, 500);

    QString lastError = QStringLiteral("多维度解析失败");
    int lastStatusCode = 0;
    for (int index = 0; index < plans.size(); ++index) {
        const auto &plan = plans.at(index);
        auto analyses = attemptDimensions(plan.first, plan.second, &lastError, &lastStatusCode);
        if (analyses.has_value()) {
            if (httpStatusCode) {
                *httpStatusCode = lastStatusCode;
            }
            return analyses;
        }

        if (index >= plans.size() - 1 || !shouldRetrySummaryFailure(lastStatusCode, lastError)) {
            break;
        }
    }

    if (httpStatusCode) {
        *httpStatusCode = lastStatusCode;
    }
    if (errorMessage) {
        *errorMessage = lastError;
    }
    return std::nullopt;
}
