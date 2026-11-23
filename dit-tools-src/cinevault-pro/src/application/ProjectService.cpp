#include "application/ProjectService.h"

#include "infrastructure/config/AppSettings.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "infrastructure/logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
constexpr auto kProjectDatabaseFileName = "project.cvdb";
constexpr auto kProjectMarkerFileName = ".cinevault-project";

QString projectDatabasePath(const QString &projectPath)
{
    const QFileInfo info(projectPath.trimmed());
    const auto databasePath = info.isDir()
        ? QDir(info.absoluteFilePath()).filePath(QString::fromLatin1(kProjectDatabaseFileName))
        : info.absoluteFilePath();
    return QFileInfo(databasePath).absoluteFilePath();
}

QString fallbackProjectName(const QString &databasePath)
{
    const QFileInfo databaseInfo(databasePath);
    const auto rootName = QDir(databaseInfo.absolutePath()).dirName();
    return rootName.isEmpty() ? databaseInfo.completeBaseName() : rootName;
}

bool validateProjectName(const QString &projectName, QString *errorMessage)
{
    const auto safeName = projectName.trimmed();
    if (safeName.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目名称不能为空。");
        }
        return false;
    }
    if (safeName == QStringLiteral(".") || safeName == QStringLiteral("..")
        || safeName.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目名称包含非法字符。");
        }
        return false;
    }
    return true;
}

bool ensureProjectDirectories(const QString &projectRoot, QString *errorMessage)
{
    const QStringList directories = {
        QStringLiteral("exports"),
        QStringLiteral("logs"),
        QStringLiteral("reports"),
        QStringLiteral("cache/thumbnails"),
        QStringLiteral("cache/report-preview"),
        QStringLiteral("analysis/frames")
    };

    QDir dir;
    for (const auto &path : directories) {
        const auto absolutePath = QDir(projectRoot).filePath(path);
        if (!dir.mkpath(absolutePath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法创建项目目录：%1").arg(absolutePath);
            }
            return false;
        }
    }
    return true;
}

bool readProjectRecord(const QString &databasePath, Project *project, QString *errorMessage)
{
    if (!project) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目记录为空。");
        }
        return false;
    }

    const auto connectionName = QStringLiteral("cinevault_project_read_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db;
    {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("打开项目数据库失败：%1").arg(db.lastError().text());
            }
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT id, name, root_path, created_at FROM project ORDER BY created_at DESC LIMIT 1"));
        if (!query.exec() || !query.next()) {
            if (errorMessage) {
                *errorMessage = query.lastError().text().isEmpty()
                    ? QStringLiteral("项目数据库缺少项目信息。")
                    : QStringLiteral("读取项目信息失败：%1").arg(query.lastError().text());
            }
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        project->id = query.value(0).toString();
        project->name = query.value(1).toString();
        project->rootPath = query.value(2).toString();
        project->databasePath = databasePath;
        project->createdAt = query.value(3).toString();
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

bool writeProjectRecordToDatabase(const QString &databasePath, const Project &project, QString *errorMessage)
{
    const auto connectionName = QStringLiteral("cinevault_project_write_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db;
    {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("打开项目数据库失败：%1").arg(db.lastError().text());
            }
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        if (!db.transaction()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery clear(db);
        if (!clear.exec(QStringLiteral("DELETE FROM project"))) {
            if (errorMessage) {
                *errorMessage = clear.lastError().text();
            }
            db.rollback();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT OR REPLACE INTO project (id, name, root_path, created_at) VALUES (?, ?, ?, ?)"));
        insert.addBindValue(project.id);
        insert.addBindValue(project.name);
        insert.addBindValue(project.rootPath);
        insert.addBindValue(project.createdAt);
        if (!insert.exec()) {
            if (errorMessage) {
                *errorMessage = insert.lastError().text();
            }
            db.rollback();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }
        if (!db.commit()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
            db.rollback();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

bool samePath(const QString &left, const QString &right)
{
    return QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(), Qt::CaseInsensitive) == 0;
}

bool pathIsInside(const QString &candidate, const QString &root)
{
    auto normalizedCandidate = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath()).replace(QLatin1Char('\\'), QLatin1Char('/'));
    auto normalizedRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath()).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!normalizedRoot.endsWith(QLatin1Char('/'))) {
        normalizedRoot.append(QLatin1Char('/'));
    }
    return normalizedCandidate.startsWith(normalizedRoot, Qt::CaseInsensitive);
}

bool renameDirectory(const QString &oldRoot, const QString &newRoot, QString *errorMessage)
{
    QDir dir;
    if (dir.rename(oldRoot, newRoot)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("无法移动项目文件夹：%1").arg(oldRoot);
    }
    return false;
}

bool writeProjectMarker(const QString &projectRoot, const QString &projectId, QString *errorMessage)
{
    QSaveFile marker(QDir(projectRoot).filePath(QString::fromLatin1(kProjectMarkerFileName)));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Text)
        || marker.write(projectId.toUtf8()) != projectId.toUtf8().size()
        || !marker.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入项目根标记：%1").arg(marker.errorString());
        }
        return false;
    }
    return true;
}

bool isDedicatedProjectRoot(const QString &databasePath,
                            const Project &project,
                            QString *errorMessage)
{
    const QFileInfo databaseInfo(databasePath);
    const auto projectRoot = databaseInfo.absolutePath();
    if (databaseInfo.fileName().compare(QString::fromLatin1(kProjectDatabaseFileName), Qt::CaseInsensitive) != 0
        || QDir(projectRoot).isRoot()
        || !samePath(project.rootPath, projectRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("拒绝删除：该数据库不位于经过验证的专用项目根目录。可改用“从项目库移除”。");
        }
        return false;
    }

    QFile marker(QDir(projectRoot).filePath(QString::fromLatin1(kProjectMarkerFileName)));
    if (!marker.open(QIODevice::ReadOnly | QIODevice::Text)
        || QString::fromUtf8(marker.readAll()).trimmed() != project.id) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("拒绝删除：项目根标记缺失或不匹配。为保护同目录中的其他文件，只能移除项目库入口。");
        }
        return false;
    }
    return true;
}
}

ProjectService::ProjectService(DatabaseManager *databaseManager,
                               AppSettings *settings,
                               GlobalDatabaseManager *globalDatabaseManager,
                               QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_settings(settings)
    , m_globalDatabaseManager(globalDatabaseManager)
{
}

bool ProjectService::createProject(const QString &projectName, const QString &parentDirectory, QString *errorMessage)
{
    const auto safeName = projectName.trimmed();
    if (!validateProjectName(safeName, errorMessage)) {
        return false;
    }
    if (parentDirectory.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目保存目录不能为空。");
        }
        return false;
    }

    const auto projectRoot = QFileInfo(QDir(parentDirectory).filePath(safeName)).absoluteFilePath();
    const auto databasePath = QDir(projectRoot).filePath(QString::fromLatin1(kProjectDatabaseFileName));
    if (QFileInfo::exists(projectRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目标项目文件夹已存在：%1").arg(projectRoot);
        }
        return false;
    }

    if (!ensureProjectDirectories(projectRoot, errorMessage)) {
        return false;
    }

    DatabaseManager preparationDatabase;
    if (!preparationDatabase.openProjectDatabase(databasePath, errorMessage)) {
        QDir(projectRoot).removeRecursively();
        return false;
    }
    preparationDatabase.closeProjectDatabase();

    Project project;
    project.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    project.name = safeName;
    project.rootPath = projectRoot;
    project.databasePath = databasePath;
    project.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (!writeProjectRecordToDatabase(databasePath, project, errorMessage)
        || !writeProjectMarker(projectRoot, project.id, errorMessage)) {
        QDir(projectRoot).removeRecursively();
        return false;
    }

    if (!openProject(databasePath, errorMessage)) {
        QDir(projectRoot).removeRecursively();
        return false;
    }
    Logger::info(QStringLiteral("项目已创建：%1").arg(project.rootPath));
    return true;
}

bool ProjectService::openProject(const QString &projectPath, QString *errorMessage)
{
    const auto databasePath = projectDatabasePath(projectPath);

    if (!QFileInfo::exists(databasePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到项目数据库：%1").arg(databasePath);
        }
        return false;
    }

    Project storedProject;
    if (!readProjectRecord(databasePath, &storedProject, errorMessage)) {
        return false;
    }

    Project project = storedProject;
    project.databasePath = databasePath;
    project.rootPath = QFileInfo(databasePath).absolutePath();
    if (!ensureProjectDirectories(project.rootPath, errorMessage)) {
        return false;
    }

    const auto previousProject = m_currentProject;
    emit projectAboutToChange(previousProject.databasePath, databasePath);
    const auto restorePreviousProject = [&]() {
        m_databaseManager->closeProjectDatabase();
        m_currentProject = {};
        if (previousProject.databasePath.isEmpty()) {
            emit projectChanged();
            return;
        }
        QString restoreError;
        if (m_databaseManager->openProjectDatabase(previousProject.databasePath, &restoreError)) {
            m_currentProject = previousProject;
        } else if (errorMessage) {
            *errorMessage += QStringLiteral("；恢复原项目失败：%1").arg(restoreError);
        }
        emit projectChanged();
    };

    if (!m_databaseManager->openProjectDatabase(databasePath, errorMessage)) {
        restorePreviousProject();
        return false;
    }
    const bool repairedPhysicalRoot = !samePath(storedProject.rootPath, project.rootPath);
    if (repairedPhysicalRoot && !writeProjectRecord(project, errorMessage)) {
        restorePreviousProject();
        return false;
    }

    m_currentProject = project;
    m_settings->addKnownProject(databasePath);
    m_settings->addRecentProject(databasePath);
    Logger::info(QStringLiteral("项目已打开：%1").arg(databasePath));
    emit projectChanged();
    return true;
}

void ProjectService::closeProject()
{
    emit projectAboutToChange(m_currentProject.databasePath, QString());
    if (!m_currentProject.databasePath.isEmpty()) {
        Logger::info(QStringLiteral("项目已关闭：%1").arg(m_currentProject.databasePath));
    }
    m_databaseManager->closeProjectDatabase();
    m_currentProject = {};
    emit projectChanged();
}

Project ProjectService::currentProject() const
{
    return m_currentProject;
}

bool ProjectService::projectForPath(const QString &projectPath, Project *project, QString *errorMessage) const
{
    return readProjectRecord(projectDatabasePath(projectPath), project, errorMessage);
}

QVector<ProjectLibraryEntry> ProjectService::projectLibraryEntries() const
{
    QVector<ProjectLibraryEntry> entries;
    QSet<QString> seen;

    auto appendPath = [this, &entries, &seen](const QString &path) {
        const auto databasePath = projectDatabasePath(path);
        if (databasePath.isEmpty() || seen.contains(databasePath)) {
            return;
        }
        seen.insert(databasePath);
        entries.append(buildProjectLibraryEntry(databasePath));
    };

    for (const auto &path : m_settings->recentProjects()) {
        appendPath(path);
    }
    for (const auto &path : m_settings->knownProjects()) {
        appendPath(path);
    }
    return entries;
}

QStringList ProjectService::recentProjects() const
{
    return m_settings->recentProjects();
}

bool ProjectService::hasOpenProject() const
{
    return !m_currentProject.databasePath.isEmpty();
}

void ProjectService::removeProjectFromLibrary(const QString &projectPath)
{
    m_settings->removeKnownProject(projectDatabasePath(projectPath));
    emit projectLibraryChanged();
}

bool ProjectService::renameProject(const QString &projectPath, const QString &newProjectName, QString *errorMessage)
{
    const auto databasePath = projectDatabasePath(projectPath);
    const auto safeName = newProjectName.trimmed();
    if (!validateProjectName(safeName, errorMessage)) {
        return false;
    }
    if (!QFileInfo::exists(databasePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到项目数据库：%1").arg(databasePath);
        }
        return false;
    }

    Project project;
    if (!readProjectRecord(databasePath, &project, errorMessage)) {
        return false;
    }

    const auto oldRoot = QFileInfo(databasePath).absolutePath();
    const auto newRoot = QDir(QFileInfo(oldRoot).absolutePath()).filePath(safeName);
    const auto newDatabasePath = QDir(newRoot).filePath(QStringLiteral("project.cvdb"));
    const bool folderUnchanged = samePath(oldRoot, newRoot);
    if (!folderUnchanged && QFileInfo::exists(newRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目标项目文件夹已存在：%1").arg(newRoot);
        }
        return false;
    }

    const bool wasCurrent = !m_currentProject.databasePath.isEmpty() && samePath(m_currentProject.databasePath, databasePath);
    if (wasCurrent) {
        closeProject();
    }

    if (!folderUnchanged && !renameDirectory(oldRoot, newRoot, errorMessage)) {
        if (wasCurrent) {
            QString reopenError;
            openProject(databasePath, &reopenError);
        }
        return false;
    }

    const auto originalProject = project;
    project.name = safeName;
    project.rootPath = newRoot;
    project.databasePath = newDatabasePath;
    if (!ensureProjectDirectories(project.rootPath, errorMessage)
        || !writeProjectRecordToDatabase(project.databasePath, project, errorMessage)) {
        const auto operationError = errorMessage ? *errorMessage : QStringLiteral("项目记录更新失败");
        writeProjectRecordToDatabase(newDatabasePath, originalProject, nullptr);
        QString rollbackError;
        if (!folderUnchanged && !renameDirectory(newRoot, oldRoot, &rollbackError) && errorMessage) {
            *errorMessage = QStringLiteral("%1；目录回滚失败：%2").arg(operationError, rollbackError);
        } else if (errorMessage) {
            *errorMessage = operationError;
        }
        if (wasCurrent) {
            QString reopenError;
            if (!openProject(databasePath, &reopenError) && errorMessage) {
                *errorMessage += QStringLiteral("；恢复原项目失败：%1").arg(reopenError);
            }
        }
        return false;
    }

    bool globalReferenceUpdated = false;
    if (m_globalDatabaseManager) {
        QString globalError;
        globalReferenceUpdated = m_globalDatabaseManager->updateProjectReference(
            project.id, project.name, databasePath, project.databasePath, &globalError);
        if (!globalReferenceUpdated) {
            writeProjectRecordToDatabase(newDatabasePath, originalProject, nullptr);
            if (!folderUnchanged) {
                renameDirectory(newRoot, oldRoot, nullptr);
            }
            if (wasCurrent) {
                QString reopenError;
                openProject(databasePath, &reopenError);
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("更新全局项目引用失败：%1").arg(globalError);
            }
            return false;
        }
    }

    if (wasCurrent) {
        if (!openProject(project.databasePath, errorMessage)) {
            if (globalReferenceUpdated) {
                m_globalDatabaseManager->updateProjectReference(
                    originalProject.id,
                    originalProject.name,
                    project.databasePath,
                    databasePath,
                    nullptr);
            }
            writeProjectRecordToDatabase(newDatabasePath, originalProject, nullptr);
            if (!folderUnchanged) {
                renameDirectory(newRoot, oldRoot, nullptr);
            }
            QString reopenError;
            openProject(databasePath, &reopenError);
            return false;
        }
    }
    m_settings->replaceProjectPath(databasePath, project.databasePath);
    emit projectLibraryChanged();
    return true;
}

bool ProjectService::moveProject(const QString &projectPath, const QString &newParentDirectory, QString *errorMessage)
{
    const auto databasePath = projectDatabasePath(projectPath);
    if (!QFileInfo::exists(databasePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到项目数据库：%1").arg(databasePath);
        }
        return false;
    }
    if (newParentDirectory.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目标位置不能为空。");
        }
        return false;
    }

    Project project;
    if (!readProjectRecord(databasePath, &project, errorMessage)) {
        return false;
    }

    const auto oldRoot = QFileInfo(databasePath).absolutePath();
    const auto projectFolderName = QFileInfo(oldRoot).fileName();
    const auto targetParent = QFileInfo(newParentDirectory).absoluteFilePath();
    const auto newRoot = QDir(targetParent).filePath(projectFolderName);
    const auto newDatabasePath = QDir(newRoot).filePath(QStringLiteral("project.cvdb"));

    if (samePath(oldRoot, newRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("项目已位于所选位置。");
        }
        return false;
    }
    if (pathIsInside(targetParent, oldRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("不能把项目移动到自身文件夹内部。");
        }
        return false;
    }
    if (QFileInfo::exists(newRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目标项目文件夹已存在：%1").arg(newRoot);
        }
        return false;
    }
    QDir dir;
    if (!dir.mkpath(targetParent)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建目标位置：%1").arg(targetParent);
        }
        return false;
    }

    const bool wasCurrent = !m_currentProject.databasePath.isEmpty() && samePath(m_currentProject.databasePath, databasePath);
    if (wasCurrent) {
        closeProject();
    }

    if (!renameDirectory(oldRoot, newRoot, errorMessage)) {
        if (wasCurrent) {
            QString reopenError;
            openProject(databasePath, &reopenError);
        }
        return false;
    }

    const auto originalProject = project;
    project.rootPath = newRoot;
    project.databasePath = newDatabasePath;
    if (!ensureProjectDirectories(project.rootPath, errorMessage)
        || !writeProjectRecordToDatabase(project.databasePath, project, errorMessage)) {
        const auto operationError = errorMessage ? *errorMessage : QStringLiteral("项目记录更新失败");
        writeProjectRecordToDatabase(newDatabasePath, originalProject, nullptr);
        QString rollbackError;
        if (!renameDirectory(newRoot, oldRoot, &rollbackError) && errorMessage) {
            *errorMessage = QStringLiteral("%1；目录回滚失败：%2").arg(operationError, rollbackError);
        } else if (errorMessage) {
            *errorMessage = operationError;
        }
        if (wasCurrent) {
            QString reopenError;
            if (!openProject(databasePath, &reopenError) && errorMessage) {
                *errorMessage += QStringLiteral("；恢复原项目失败：%1").arg(reopenError);
            }
        }
        return false;
    }

    bool globalReferenceUpdated = false;
    if (m_globalDatabaseManager) {
        QString globalError;
        globalReferenceUpdated = m_globalDatabaseManager->updateProjectReference(
            project.id, project.name, databasePath, project.databasePath, &globalError);
        if (!globalReferenceUpdated) {
            writeProjectRecordToDatabase(newDatabasePath, originalProject, nullptr);
            renameDirectory(newRoot, oldRoot, nullptr);
            if (wasCurrent) {
                QString reopenError;
                openProject(databasePath, &reopenError);
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("更新全局项目引用失败：%1").arg(globalError);
            }
            return false;
        }
    }

    if (wasCurrent) {
        if (!openProject(project.databasePath, errorMessage)) {
            if (globalReferenceUpdated) {
                m_globalDatabaseManager->updateProjectReference(
                    originalProject.id,
                    originalProject.name,
                    project.databasePath,
                    databasePath,
                    nullptr);
            }
            writeProjectRecordToDatabase(newDatabasePath, originalProject, nullptr);
            renameDirectory(newRoot, oldRoot, nullptr);
            QString reopenError;
            openProject(databasePath, &reopenError);
            return false;
        }
    }
    m_settings->replaceProjectPath(databasePath, project.databasePath);
    emit projectLibraryChanged();
    return true;
}

bool ProjectService::deleteProjectToTrash(const QString &projectPath, QString *errorMessage)
{
    const auto databasePath = projectDatabasePath(projectPath);
    if (!QFileInfo::exists(databasePath)) {
        m_settings->removeKnownProject(databasePath);
        if (m_globalDatabaseManager) {
            m_globalDatabaseManager->removeProjectReference(QString(), databasePath, nullptr);
        }
        emit projectLibraryChanged();
        return true;
    }

    Project project;
    if (!readProjectRecord(databasePath, &project, errorMessage)) {
        return false;
    }
    if (!isDedicatedProjectRoot(databasePath, project, errorMessage)) {
        return false;
    }

    const auto projectRoot = QFileInfo(databasePath).absolutePath();
    const bool wasCurrent = !m_currentProject.databasePath.isEmpty() && samePath(m_currentProject.databasePath, databasePath);
    if (wasCurrent) {
        closeProject();
    }

    QString trashPath;
    if (!QFile::moveToTrash(projectRoot, &trashPath)) {
        if (wasCurrent) {
            QString reopenError;
            openProject(databasePath, &reopenError);
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法将项目移入回收站：%1").arg(projectRoot);
        }
        return false;
    }

    m_settings->removeKnownProject(databasePath);
    if (m_globalDatabaseManager) {
        m_globalDatabaseManager->removeProjectReference(project.id, databasePath, nullptr);
    }
    emit projectLibraryChanged();
    return true;
}

bool ProjectService::writeProjectRecord(const Project &project, QString *errorMessage)
{
    auto db = m_databaseManager->database();
    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM project"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        db.rollback();
        return false;
    }

    query.prepare(QStringLiteral("INSERT OR REPLACE INTO project (id, name, root_path, created_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(project.id);
    query.addBindValue(project.name);
    query.addBindValue(project.rootPath);
    query.addBindValue(project.createdAt);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        db.rollback();
        return false;
    }
    return true;
}

ProjectLibraryEntry ProjectService::buildProjectLibraryEntry(const QString &projectPath) const
{
    const auto databasePath = projectDatabasePath(projectPath);
    ProjectLibraryEntry entry;
    entry.databasePath = databasePath;
    entry.rootPath = QFileInfo(databasePath).absolutePath();
    entry.name = fallbackProjectName(databasePath);
    entry.available = QFileInfo::exists(databasePath);
    entry.current = !m_currentProject.databasePath.isEmpty()
        && projectDatabasePath(m_currentProject.databasePath) == databasePath;

    if (!entry.available) {
        return entry;
    }

    const auto connectionName = QStringLiteral("cinevault_project_library_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db;
    {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT name, root_path, created_at FROM project ORDER BY created_at DESC LIMIT 1"));
            if (query.exec() && query.next()) {
                entry.name = query.value(0).toString();
                entry.rootPath = query.value(1).toString();
                entry.createdAt = query.value(2).toString();
            }
        }
        db.close();
    }
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);

    return entry;
}
