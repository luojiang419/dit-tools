#pragma once

#include "domain/SearchTypes.h"

#include <QDate>
#include <QObject>
#include <QString>

#include <optional>

class SearchAssistantClient : public QObject {
public:
    using QObject::QObject;

    std::optional<ModelSearchUnderstanding> understandQuery(
        const QString &queryText,
        const QDate &referenceDate,
        const QString &baseUrl,
        const QString &model,
        int timeoutSec,
        QString *errorMessage = nullptr,
        int *httpStatusCode = nullptr) const;
};
