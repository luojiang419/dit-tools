#include "infrastructure/raw/RawPreviewProtocol.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

QByteArray RawPreviewProtocol::encodeMessage(const QJsonObject &message, QString *errorMessage)
{
    const auto payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > MaximumPayloadBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RAW worker 消息大小无效：%1 字节").arg(payload.size());
        }
        return {};
    }

    QByteArray frame(4, '\0');
    qToBigEndian(static_cast<quint32>(payload.size()),
                 reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);
    return frame;
}

RawPreviewProtocol::ReadStatus RawPreviewProtocol::tryTakeMessage(QByteArray *buffer,
                                                                  QJsonObject *message,
                                                                  QString *errorMessage)
{
    if (!buffer || !message) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RAW worker 协议缓冲区未初始化");
        }
        return ReadStatus::InvalidFrame;
    }
    if (buffer->size() < 4) {
        return ReadStatus::NeedMoreData;
    }

    const auto payloadSize = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(buffer->constData()));
    if (payloadSize == 0 || payloadSize > static_cast<quint32>(MaximumPayloadBytes)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RAW worker 帧长度越界：%1").arg(payloadSize);
        }
        return ReadStatus::InvalidFrame;
    }
    if (buffer->size() < 4 + static_cast<qsizetype>(payloadSize)) {
        return ReadStatus::NeedMoreData;
    }

    const auto payload = buffer->mid(4, payloadSize);
    buffer->remove(0, 4 + payloadSize);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("RAW worker JSON 无效：%1").arg(parseError.errorString());
        }
        return ReadStatus::InvalidFrame;
    }

    *message = document.object();
    return ReadStatus::MessageReady;
}

QJsonObject RawPreviewProtocol::successResponse(const QString &requestId, const QJsonObject &result)
{
    return {
        {QStringLiteral("protocolVersion"), ProtocolVersion},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("ok"), true},
        {QStringLiteral("result"), result},
    };
}

QJsonObject RawPreviewProtocol::errorResponse(const QString &requestId,
                                              const QString &code,
                                              const QString &message,
                                              bool retryable)
{
    return {
        {QStringLiteral("protocolVersion"), ProtocolVersion},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{
             {QStringLiteral("code"), code},
             {QStringLiteral("message"), message},
             {QStringLiteral("retryable"), retryable},
         }},
    };
}
