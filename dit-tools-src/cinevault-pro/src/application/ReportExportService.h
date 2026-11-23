#pragma once

#include "core/report/ReportModels.h"

#include <QObject>
#include <QString>
#include <QStringList>

class DatabaseManager;
class ProjectService;
class QSqlDatabase;

struct ReportExportRequest {
    qint64 sourceRootId = 0;
    QString shootTime;
    QString location;
    QString director;
    QString cinematographer;
    QString ditName;
    QString outputPath;
    ReportSectionOptions sections;
};

class ReportExportService : public QObject {
    Q_OBJECT

public:
    explicit ReportExportService(DatabaseManager *databaseManager, ProjectService *projectService, QObject *parent = nullptr);

    bool hasOpenProject() const;
    QString defaultReportPath() const;
    bool exportPdf(const ReportExportRequest &request, QString *outputPath, QString *errorMessage) const;
    bool generatePreview(const ReportExportRequest &request, QStringList *pagePaths, QString *errorMessage) const;

private:
    bool buildDocument(const ReportExportRequest &request, ReportDocument *document, QString *errorMessage) const;
    bool fetchSources(QSqlDatabase &db, qint64 sourceRootId, ReportDocument *document, QString *errorMessage) const;
    bool fetchAssets(QSqlDatabase &db, qint64 sourceRootId, ReportDocument *document, QString *errorMessage) const;
    bool fetchStreams(QSqlDatabase &db, ReportDocument *document, QString *errorMessage) const;
    void buildTree(ReportDocument *document) const;
    void updateTotals(ReportDocument *document) const;

    DatabaseManager *m_databaseManager = nullptr;
    ProjectService *m_projectService = nullptr;
};
