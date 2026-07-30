#pragma once

#include "domain/Entities.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace VisionResponseParser {

struct AssistantResponseEnvelope {
    QString content;
    qsizetype reasoningLength = 0;
    QString finishReason;
    qint64 promptTokens = -1;
    qint64 completionTokens = -1;
    qint64 totalTokens = -1;
};

std::optional<AssistantResponseEnvelope> extractAssistantEnvelope(const QByteArray &responseBody,
                                                                  QString *errorMessage);
std::optional<QString> extractAssistantContent(const QByteArray &responseBody, QString *errorMessage);
std::optional<QJsonObject> parseAssistantJson(const QByteArray &responseBody, QString *errorMessage);
std::optional<VisionFrameAnalysis> normalizeFrameAnalysis(const QJsonObject &payload, QString *errorMessage);
std::optional<VisionVideoSummary> normalizeVideoSummary(const QJsonObject &payload, QString *errorMessage);
std::optional<VisionFrameAnalysis> fallbackFrameAnalysisFromContent(const QString &content, QString *errorMessage);
std::optional<VisionVideoSummary> fallbackVideoSummaryFromContent(const QString &content, QString *errorMessage);

}
