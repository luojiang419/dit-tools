#pragma once

#include "core/search/NaturalLanguageQueryParser.h"
#include "domain/SearchTypes.h"

#include <QDate>
#include <QString>

class GlobalDatabaseManager;
class SemanticSearchProvider;

class SearchEngine {
public:
    explicit SearchEngine(GlobalDatabaseManager *globalDatabaseManager = nullptr,
                          SemanticSearchProvider *semanticSearchProvider = nullptr);

    QString buildLikePattern(const QString &keyword) const;
    QString buildFtsQuery(const QString &keyword) const;
    HybridSearchResult searchMaterials(const QString &queryText,
                                       const MaterialSearchScope &scope = {},
                                       const QDate &referenceDate = QDate::currentDate(),
                                       const ModelSearchUnderstanding *modelUnderstanding = nullptr) const;

    void setMaterialSearchDependencies(GlobalDatabaseManager *globalDatabaseManager,
                                       SemanticSearchProvider *semanticSearchProvider);

private:
    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    SemanticSearchProvider *m_semanticSearchProvider = nullptr;
    NaturalLanguageQueryParser m_queryParser;
};
