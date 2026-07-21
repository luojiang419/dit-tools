#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QStringList>

class DatabaseManager;
class JobService;
class ScanEngine;

class ImportService : public QObject {
    Q_OBJECT

public:
    explicit ImportService(DatabaseManager *databaseManager, JobService *jobService, ScanEngine *scanEngine, QObject *parent = nullptr);

    bool importDirectory(const QString &directoryPath, QString *errorMessage);
    bool rescanSourceRoot(qint64 sourceRootId,
                          const QString &reason,
                          QString *errorMessage = nullptr);
    bool rescanSourceDirectories(qint64 sourceRootId,
                                 const QStringList &changedPaths,
                                 bool forceFullScan,
                                 const QString &reason,
                                 QString *errorMessage = nullptr);
    QVector<SourceRoot> sourceRoots() const;
    QString lastMessage() const;

public slots:
    void resumeInterruptedScans();
    void rescanLegacySourceRoots();

signals:
    void importStateChanged();
    void catalogChanged();
    void sourceRootsChanged();
    void sourceScanFinished(qint64 sourceRootId);
    void sourceScanFailed(qint64 sourceRootId, const QString &message);

private:
    DatabaseManager *m_databaseManager = nullptr;
    JobService *m_jobService = nullptr;
    ScanEngine *m_scanEngine = nullptr;
    QString m_lastMessage;

    bool startSourceScan(const SourceRoot &sourceRoot,
                         const QString &titlePrefix,
                         const QString &detail,
                         const QStringList &dirtyDirectoryPaths = {},
                         QString *errorMessage = nullptr);
};
