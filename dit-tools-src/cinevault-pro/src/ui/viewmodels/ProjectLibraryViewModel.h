#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QVector>

class ProjectLibraryListModel;
class ProjectService;

class ProjectLibraryViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(ProjectLibraryListModel* model READ model CONSTANT)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY stateChanged)

public:
    explicit ProjectLibraryViewModel(ProjectService *projectService, QObject *parent = nullptr);

    ProjectLibraryListModel *model() const;
    QString searchText() const;
    QString statusText() const;
    QString lastMessage() const;

    void setSearchText(const QString &text);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void createProject();
    Q_INVOKABLE void openExternalProject();
    Q_INVOKABLE void openProject(const QString &databasePath);
    Q_INVOKABLE void removeProject(const QString &databasePath);
    Q_INVOKABLE void renameProject(const QString &databasePath, const QString &currentName);
    Q_INVOKABLE void moveProject(const QString &databasePath);
    Q_INVOKABLE void deleteProject(const QString &databasePath, bool available);

signals:
    void stateChanged();
    void searchTextChanged();
    void projectActivated();

private:
    void refresh();
    void setLastMessage(const QString &message);

    ProjectService *m_projectService = nullptr;
    ProjectLibraryListModel *m_model = nullptr;
    QVector<ProjectLibraryEntry> m_allProjects;
    QString m_searchText;
    QString m_lastMessage;
};
