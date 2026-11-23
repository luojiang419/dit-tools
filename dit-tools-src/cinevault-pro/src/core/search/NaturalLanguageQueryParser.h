#pragma once

#include "domain/SearchTypes.h"

#include <QDate>

class NaturalLanguageQueryParser {
public:
    ParsedMaterialQuery parse(const QString &text,
                              const QDate &referenceDate = QDate::currentDate()) const;
};
