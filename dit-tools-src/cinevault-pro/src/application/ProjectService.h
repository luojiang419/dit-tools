#pragma once

#include "domain/Entities.h"

#include <QObject>

class AppSettings;
class DatabaseManager;
class GlobalDatabaseManager;

class ProjectService : public QObject {
    Q_OBJECT

public:
    explicit ProjectService(DatabaseManager *databaseManager,
                            AppSettings *settings,
                            GlobalDatabaseManager *globalDatabaseManager = nullptr,
                            QObject *parent = nullptr);

    bool createProject(const QString &projectName, const QString &parentDirectory, QString *errorMessage);
    bool openProject(const QString &projectPath, QString *errorMessage);
    void closeProject();

    Project currentProject() const;
    bool projectForPath(const QString &projectPath, Project *project, QString *errorMessage) const;
    QVector<ProjectLibraryEntry> projectLibraryEntries() const;
    QStringList recentProjects() const;
    bool hasOpenProject() const;
    void removeProjectFromLibrary(const QString &projectPath);
    bool renameProject(const QString &projectPath, const QString &newProjectName, QString *errorMessage);
    bool moveProject(const QString &projectPath, const QString &newParentDirectory, QString *errorMessage);
    bool deleteProjectToTrash(const QString &projectPath, QString *errorMessage);

signals:
    void projectChanged();
    void projectLibraryChanged();

private:
    bool writeProjectRecord(const Project &project, QString *errorMessage);
    ProjectLibraryEntry buildProjectLibraryEntry(const QString &projectPath) const;

    DatabaseManager *m_databaseManager = nullptr;
    AppSettings *m_settings = nullptr;
    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    Project m_currentProject;
};
