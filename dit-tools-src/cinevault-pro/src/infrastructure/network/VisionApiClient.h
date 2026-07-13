#pragma once

#include "domain/Entities.h"

#include <optional>

class VisionApiClient {
public:
    bool testConnection(const QString &baseUrl,
                        const QString &apiKey,
                        const QString &model,
                        int timeoutSec,
                        QString *errorMessage) const;

    std::optional<VisionFrameAnalysis> analyzeFrame(const QString &imagePath,
                                                    const QString &sourceFileName,
                                                    const QString &baseUrl,
                                                    const QString &apiKey,
                                                    const QString &model,
                                                    int timeoutSec,
                                                    QString *errorMessage,
                                                    int *httpStatusCode = nullptr) const;

    std::optional<QVector<MaterialDimensionAnalysis>> analyzeFrameDimensions(const QString &imagePath,
                                                                             const QString &sourceFileName,
                                                                             const QString &frameContext,
                                                                             const QStringList &dimensions,
                                                                             const QString &baseUrl,
                                                                             const QString &apiKey,
                                                                             const QString &model,
                                                                             int timeoutSec,
                                                                             QString *errorMessage,
                                                                             int *httpStatusCode = nullptr) const;

    std::optional<VisionVideoSummary> analyzeImage(const QString &imagePath,
                                                   const QString &fileName,
                                                   const QString &baseUrl,
                                                   const QString &apiKey,
                                                   const QString &model,
                                                   int timeoutSec,
                                                   QString *errorMessage,
                                                   int *httpStatusCode = nullptr) const;

    std::optional<VisionVideoSummary> summarizeText(const QString &fileName,
                                                    const QString &text,
                                                    const QString &baseUrl,
                                                    const QString &apiKey,
                                                    const QString &model,
                                                    int timeoutSec,
                                                    QString *errorMessage,
                                                    int *httpStatusCode = nullptr) const;

    std::optional<VisionVideoSummary> summarizeVideo(const QString &fileName,
                                                     const QVector<FrameAnalysisRecord> &frames,
                                                     const QString &baseUrl,
                                                     const QString &apiKey,
                                                     const QString &model,
                                                     int timeoutSec,
                                                     QString *errorMessage,
                                                     int *attemptCount = nullptr,
                                                     int *httpStatusCode = nullptr) const;

    std::optional<QVector<MaterialDimensionAnalysis>> analyzeDimensions(const QString &fileName,
                                                                        const QString &baseContext,
                                                                        const QStringList &dimensions,
                                                                        const QString &baseUrl,
                                                                        const QString &apiKey,
                                                                        const QString &model,
                                                                        int timeoutSec,
                                                                        QString *errorMessage,
                                                                        int *httpStatusCode = nullptr) const;
};
