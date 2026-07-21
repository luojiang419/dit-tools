#pragma once

#include <QByteArray>
#include <QJsonObject>

namespace RawPreviewProtocol {

inline constexpr int ProtocolVersion = 1;
inline constexpr qsizetype MaximumPayloadBytes = 1024 * 1024;

enum class ReadStatus {
    NeedMoreData = 0,
    MessageReady,
    InvalidFrame
};

QByteArray encodeMessage(const QJsonObject &message, QString *errorMessage = nullptr);
ReadStatus tryTakeMessage(QByteArray *buffer,
                          QJsonObject *message,
                          QString *errorMessage = nullptr);
QJsonObject successResponse(const QString &requestId, const QJsonObject &result = {});
QJsonObject errorResponse(const QString &requestId,
                          const QString &code,
                          const QString &message,
                          bool retryable);

} // namespace RawPreviewProtocol
