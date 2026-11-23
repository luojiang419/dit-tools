#pragma once

#include "domain/SearchTypes.h"

#include <QJsonObject>

#include <optional>

class SearchQueryUnderstanding {
public:
    static QJsonObject responseSchema();
    static std::optional<ModelSearchUnderstanding> parseModelPayload(
        const QJsonObject &payload,
        QString *errorMessage = nullptr);
    static ParsedMaterialQuery merge(const ParsedMaterialQuery &localQuery,
                                     const ModelSearchUnderstanding &modelUnderstanding,
                                     bool *modelApplied = nullptr);
};
