#include "application/MaterialCenterReadContext.h"

#include "application/MaterialCenterQueryService.h"
#include "core/search/SearchEngine.h"
#include "core/search/SemanticSearchIndexService.h"
#include "infrastructure/db/GlobalDatabaseManager.h"

MaterialCenterReadContext::MaterialCenterReadContext() = default;

MaterialCenterReadContext::~MaterialCenterReadContext()
{
    m_queryService.reset();
    m_searchEngine.reset();
    m_semanticIndex.reset();
    if (m_manager) {
        m_manager->closeDatabase();
    }
}

bool MaterialCenterReadContext::open(const QString &databasePath,
                                     qint64 generation,
                                     QString *errorMessage)
{
    auto manager = std::make_unique<GlobalDatabaseManager>();
    if (!manager->openReadOnlyDatabase(databasePath, errorMessage)) {
        return false;
    }
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    auto semanticIndex = std::make_unique<SemanticSearchIndexService>(manager.get());
    auto searchEngine = std::make_unique<SearchEngine>(manager.get(), semanticIndex.get());
#else
    std::unique_ptr<SemanticSearchIndexService> semanticIndex;
    auto searchEngine = std::make_unique<SearchEngine>(manager.get(), nullptr);
#endif
    auto queryService = std::make_unique<MaterialCenterQueryService>(manager.get(), searchEngine.get());

    m_databasePath = databasePath;
    m_generation = generation;
    m_manager = std::move(manager);
    m_semanticIndex = std::move(semanticIndex);
    m_searchEngine = std::move(searchEngine);
    m_queryService = std::move(queryService);
    return true;
}

MaterialCenterQueryService *MaterialCenterReadContext::queryService() const
{
    return m_queryService.get();
}

QString MaterialCenterReadContext::databasePath() const
{
    return m_databasePath;
}

qint64 MaterialCenterReadContext::generation() const
{
    return m_generation;
}

MaterialCenterReadContext *materialCenterReadContextForCurrentThread(
    const QString &databasePath,
    qint64 generation,
    QString *errorMessage)
{
    thread_local std::unique_ptr<MaterialCenterReadContext> context;
    if (!context
        || context->databasePath() != databasePath
        || context->generation() != generation) {
        auto replacement = std::make_unique<MaterialCenterReadContext>();
        if (!replacement->open(databasePath, generation, errorMessage)) {
            return nullptr;
        }
        context = std::move(replacement);
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return context.get();
}
