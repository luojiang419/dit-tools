#pragma once

#include "domain/Entities.h"

#include <QObject>

class DatabaseManager;
class JobService;
class ScanEngine;

class ImportService : public QObject {
    Q_OBJECT

public:
    explicit ImportService(DatabaseManager *databaseManager, JobService *jobService, ScanEngine *scanEngine, QObject *parent = nullptr);

    bool importDirectory(const QString &directoryPath, QString *errorMessage);
    QString lastMessage() const;

public slots:
    void rescanLegacySourceRoots();

signals:
    void importStateChanged();
    void catalogChanged();

private:
    DatabaseManager *m_databaseManager = nullptr;
    JobService *m_jobService = nullptr;
    ScanEngine *m_scanEngine = nullptr;
    QString m_lastMessage;
};
