#pragma once

#include "application/MaterialCatalogSyncService.h"
#include "domain/SearchTypes.h"

#include <QElapsedTimer>
#include <QFutureSynchronizer>
#include <QHash>
#include <QObject>
#include <QSet>

class GlobalDatabaseManager;
class IndexingWorkCoordinator;
class QTimer;
class SemanticSearchIndexService;

class SearchDocumentSyncService : public QObject {
    Q_OBJECT

public:
    explicit SearchDocumentSyncService(GlobalDatabaseManager *globalDatabaseManager,
                                       SemanticSearchIndexService *semanticSearchIndexService,
                                       QObject *parent = nullptr);
    ~SearchDocumentSyncService() override;

    bool synchronizeNow(SemanticIndexUpdateResult *result,
                        QString *errorMessage);
    bool synchronizeChangesNow(const CatalogChangeSet &changeSet,
                               SemanticIndexUpdateResult *result,
                               QString *errorMessage);
    void setWorkCoordinator(IndexingWorkCoordinator *workCoordinator);
    void waitForIdle();

public slots:
    void scheduleFullSync();
    void scheduleImmediateFullSync();
    void resumePendingWork();
    void scheduleCatalogChanges(const CatalogChangeSet &changeSet);
    void scheduleAssetSync(const QString &videoKey);

signals:
    void synchronizationProgress(int processed,
                                 int total,
                                 const QString &detail);
    void synchronizationFinished(bool success,
                                 int inserted,
                                 int updated,
                                 int unchanged,
                                 int removed,
                                 const QString &message);

private:
    struct PendingCatalogChange {
        QString projectUuid;
        CatalogChange change;
    };

    void startScheduledSync();
    void schedulePendingWork();

    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    SemanticSearchIndexService *m_semanticSearchIndexService = nullptr;
    IndexingWorkCoordinator *m_workCoordinator = nullptr;
    QTimer *m_debounceTimer = nullptr;
    QElapsedTimer m_debounceWindow;
    QFutureSynchronizer<void> m_futures;
    QHash<QString, PendingCatalogChange> m_pendingCatalogChanges;
    QSet<QString> m_pendingAnalysisVideoKeys;
    bool m_pendingFullSync = false;
    bool m_pendingImmediateFullSync = false;
    bool m_running = false;
    bool m_runningFullSync = false;
};
