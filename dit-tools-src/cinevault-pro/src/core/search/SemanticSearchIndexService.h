#pragma once

#include "core/search/BgeEmbeddingModel.h"
#include "core/search/SemanticSearchProvider.h"
#include "core/search/SemanticVectorIndex.h"
#include "domain/SearchTypes.h"

#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class GlobalDatabaseManager;

using SemanticIndexProgressCallback = std::function<void(qsizetype processed,
                                                         qsizetype total,
                                                         const QString &detail)>;

class SemanticSearchIndexService : public SemanticSearchProvider {
public:
    explicit SemanticSearchIndexService(GlobalDatabaseManager *globalDatabaseManager,
                                        QString indexFilePath = {});

    bool ensureReady(QString *errorMessage);
    bool rebuild(QString *errorMessage);
    bool invalidate(const QString &reason, QString *errorMessage);
    bool applyChanges(const QVector<SearchDocumentInput> &upserts,
                      const QStringList &removedDocumentKeys,
                      SemanticIndexUpdateResult *result,
                      QString *errorMessage,
                      const SemanticIndexProgressCallback &progressCallback = {});
    void discardLoadedIndex();
    QVector<SemanticSearchHit> search(const QString &queryText,
                                      qsizetype limit,
                                      QString *errorMessage) override;

    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] QString indexFilePath() const;

private:
    bool ensureReadyLocked(QString *errorMessage, bool allowRebuild);
    bool rebuildLocked(const QString &reason, QString *errorMessage);
    bool setStateLocked(const QString &status,
                        const QString &lastError,
                        QString *errorMessage);
    void recordFailureLocked(const QString &message);
    bool ensureIndexDirectoryLocked(QString *errorMessage) const;

    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    QString m_indexFilePath;
    BgeEmbeddingModel m_embeddingModel;
    SemanticVectorIndex m_index;
    mutable QMutex m_mutex;
    bool m_ready = false;
    bool m_lastEnsureRebuilt = false;
};
