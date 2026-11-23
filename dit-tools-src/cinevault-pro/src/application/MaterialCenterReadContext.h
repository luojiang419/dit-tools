#pragma once

#include <QString>

#include <memory>

class GlobalDatabaseManager;
class MaterialCenterQueryService;
class SearchEngine;
class SemanticSearchIndexService;

class MaterialCenterReadContext final {
public:
    MaterialCenterReadContext();
    ~MaterialCenterReadContext();

    bool open(const QString &databasePath, qint64 generation, QString *errorMessage);
    MaterialCenterQueryService *queryService() const;
    QString databasePath() const;
    qint64 generation() const;

private:
    QString m_databasePath;
    qint64 m_generation = -1;
    std::unique_ptr<GlobalDatabaseManager> m_manager;
    std::unique_ptr<SemanticSearchIndexService> m_semanticIndex;
    std::unique_ptr<SearchEngine> m_searchEngine;
    std::unique_ptr<MaterialCenterQueryService> m_queryService;
};

MaterialCenterReadContext *materialCenterReadContextForCurrentThread(
    const QString &databasePath,
    qint64 generation,
    QString *errorMessage);
