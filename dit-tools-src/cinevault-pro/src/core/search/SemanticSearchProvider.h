#pragma once

#include "domain/SearchTypes.h"

#include <QString>
#include <QVector>

class SemanticSearchProvider {
public:
    virtual ~SemanticSearchProvider() = default;

    virtual QVector<SemanticSearchHit> search(const QString &queryText,
                                              qsizetype limit,
                                              QString *errorMessage) = 0;
    [[nodiscard]] virtual bool isReady() const = 0;
};
