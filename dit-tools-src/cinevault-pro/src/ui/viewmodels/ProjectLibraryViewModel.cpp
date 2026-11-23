#include "ui/viewmodels/ProjectLibraryViewModel.h"

#include "application/ProjectService.h"
#include "shared/Paths.h"
#include "ui/models/ProjectLibraryListModel.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

namespace {
QWidget *dialogParent()
{
    return QApplication::activeWindow();
}

void showWarning(const QString &title, const QString &message)
{
    QMessageBox::warning(dialogParent(), title, message);
}

bool confirmQuestion(const QString &title, const QString &message)
{
    return QMessageBox::question(dialogParent(), title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

bool projectMatches(const ProjectLibraryEntry &project, const QString &keyword)
{
    const auto text = keyword.trimmed();
    if (text.isEmpty()) {
        return true;
    }
    return project.name.contains(text, Qt::CaseInsensitive)
        || project.rootPath.contains(text, Qt::CaseInsensitive)
        || project.databasePath.contains(text, Qt::CaseInsensitive);
}
}

ProjectLibraryViewModel::ProjectLibraryViewModel(ProjectService *projectService, QObject *parent)
    : QObject(parent)
    , m_projectService(projectService)
    , m_model(new ProjectLibraryListModel(this))
{
    connect(m_projectService, &ProjectService::projectChanged, this, &ProjectLibraryViewModel::reload);
    connect(m_projectService, &ProjectService::projectLibraryChanged, this, &ProjectLibraryViewModel::reload);
    reload();
}

ProjectLibraryListModel *ProjectLibraryViewModel::model() const
{
    return m_model;
}

QString ProjectLibraryViewModel::searchText() const
{
    return m_searchText;
}

QString ProjectLibraryViewModel::statusText() const
{
    if (m_searchText.trimmed().isEmpty()) {
        return QStringLiteral("共 %1 个项目").arg(m_model->rowCount(QModelIndex()));
    }
    return QStringLiteral("匹配 %1 个项目").arg(m_model->rowCount(QModelIndex()));
}

QString ProjectLibraryViewModel::lastMessage() const
{
    return m_lastMessage;
}

void ProjectLibraryViewModel::setSearchText(const QString &text)
{
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    emit searchTextChanged();
    refresh();
}

void ProjectLibraryViewModel::reload()
{
    m_allProjects = m_projectService->projectLibraryEntries();
    refresh();
}

void ProjectLibraryViewModel::createProject()
{
    QString errorMessage;
    bool ok = false;
    const auto projectName = QInputDialog::getText(dialogParent(),
                                                   QStringLiteral("新建项目"),
                                                   QStringLiteral("项目名称"),
                                                   QLineEdit::Normal,
                                                   QString(),
                                                   &ok);
    if (!ok) {
        setLastMessage(QStringLiteral("已取消新建项目。"));
        return;
    }
    if (projectName.trimmed().isEmpty()) {
        setLastMessage(QStringLiteral("项目名称不能为空。"));
        showWarning(QStringLiteral("新建项目"), m_lastMessage);
        return;
    }

    const auto parentDirectory = QFileDialog::getExistingDirectory(dialogParent(),
                                                                   QStringLiteral("选择项目保存目录"),
                                                                   Paths::projectsRoot());
    if (parentDirectory.isEmpty()) {
        setLastMessage(QStringLiteral("已取消选择项目保存目录。"));
        return;
    }

    if (!m_projectService->createProject(projectName, parentDirectory, &errorMessage)) {
        setLastMessage(errorMessage);
        showWarning(QStringLiteral("新建项目失败"), m_lastMessage);
        return;
    }

    setLastMessage(QStringLiteral("项目已创建：%1。点击项目卡片进入素材库。").arg(projectName.trimmed()));
    reload();
}

void ProjectLibraryViewModel::openExternalProject()
{
    const auto databasePath = QFileDialog::getOpenFileName(dialogParent(),
                                                           QStringLiteral("打开项目"),
                                                           Paths::projectsRoot(),
                                                           QStringLiteral("影资管家项目 (*.cvdb)"));
    if (databasePath.isEmpty()) {
        setLastMessage(QStringLiteral("已取消打开项目。"));
        return;
    }
    openProject(databasePath);
}

void ProjectLibraryViewModel::openProject(const QString &databasePath)
{
    QString errorMessage;
    if (!m_projectService->openProject(databasePath, &errorMessage)) {
        setLastMessage(errorMessage);
        showWarning(QStringLiteral("打开项目失败"), m_lastMessage);
        return;
    }

    setLastMessage(QStringLiteral("已打开项目：%1").arg(m_projectService->currentProject().name));
    emit projectActivated();
    reload();
}

void ProjectLibraryViewModel::removeProject(const QString &databasePath)
{
    m_projectService->removeProjectFromLibrary(databasePath);
    setLastMessage(QStringLiteral("已从项目库移除入口，项目文件未删除。"));
}

void ProjectLibraryViewModel::renameProject(const QString &databasePath, const QString &currentName)
{
    bool ok = false;
    const auto newName = QInputDialog::getText(dialogParent(),
                                               QStringLiteral("重命名项目"),
                                               QStringLiteral("新项目名称"),
                                               QLineEdit::Normal,
                                               currentName,
                                               &ok);
    if (!ok) {
        setLastMessage(QStringLiteral("已取消重命名项目。"));
        return;
    }

    QString errorMessage;
    if (!m_projectService->renameProject(databasePath, newName, &errorMessage)) {
        setLastMessage(errorMessage);
        showWarning(QStringLiteral("重命名项目失败"), m_lastMessage);
        return;
    }

    setLastMessage(QStringLiteral("项目已重命名：%1").arg(newName.trimmed()));
    reload();
}

void ProjectLibraryViewModel::moveProject(const QString &databasePath)
{
    const auto currentParent = QFileInfo(QFileInfo(databasePath).absolutePath()).absolutePath();
    const auto parentDirectory = QFileDialog::getExistingDirectory(dialogParent(),
                                                                   QStringLiteral("选择新的项目位置"),
                                                                   currentParent.isEmpty() ? Paths::projectsRoot() : currentParent);
    if (parentDirectory.isEmpty()) {
        setLastMessage(QStringLiteral("已取消移动项目。"));
        return;
    }

    QString errorMessage;
    if (!m_projectService->moveProject(databasePath, parentDirectory, &errorMessage)) {
        setLastMessage(errorMessage);
        showWarning(QStringLiteral("移动项目失败"), m_lastMessage);
        return;
    }

    setLastMessage(QStringLiteral("项目已移动到：%1").arg(parentDirectory));
    reload();
}

void ProjectLibraryViewModel::deleteProject(const QString &databasePath, bool available)
{
    const auto projectRoot = QFileInfo(databasePath).absolutePath();
    const auto message = available
        ? QStringLiteral("确定将以下整个项目目录移入系统回收站吗？\n\n%1\n\n项目库入口和素材管理中心索引会同步移除。").arg(QDir::toNativeSeparators(projectRoot))
        : QStringLiteral("项目文件不可访问。\n\n确定只从项目库移除这个缺失入口吗？");
    if (!confirmQuestion(QStringLiteral("删除项目"), message)) {
        setLastMessage(QStringLiteral("已取消删除项目。"));
        return;
    }

    QString errorMessage;
    if (!m_projectService->deleteProjectToTrash(databasePath, &errorMessage)) {
        setLastMessage(errorMessage);
        showWarning(QStringLiteral("删除项目失败"), m_lastMessage);
        return;
    }

    setLastMessage(available ? QStringLiteral("项目已移入系统回收站。") : QStringLiteral("已移除缺失项目入口。"));
    reload();
}

void ProjectLibraryViewModel::refresh()
{
    QVector<ProjectLibraryEntry> filtered;
    for (const auto &project : m_allProjects) {
        if (projectMatches(project, m_searchText)) {
            filtered.append(project);
        }
    }
    m_model->setItems(filtered);
    emit stateChanged();
}

void ProjectLibraryViewModel::setLastMessage(const QString &message)
{
    if (m_lastMessage == message) {
        return;
    }
    m_lastMessage = message;
    emit stateChanged();
}
