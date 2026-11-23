#include "ui/viewmodels/ShellViewModel.h"

#include "application/FeedbackService.h"
#include "application/ImportService.h"
#include "application/ProjectService.h"
#include "application/StorageVolumeService.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QVariantMap>

namespace {
QWidget *dialogParent()
{
    return QApplication::activeWindow();
}

void showWarning(const QString &title, const QString &message)
{
    QMessageBox::warning(dialogParent(), title, message);
}

QVariantMap workspaceTab(const QString &label, WorkspaceId workspace, int buttonWidth, bool enabled, int badgeCount = 0)
{
    return QVariantMap{
        {QStringLiteral("label"), label},
        {QStringLiteral("value"), static_cast<int>(workspace)},
        {QStringLiteral("buttonWidth"), buttonWidth},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("badgeCount"), badgeCount}
    };
}
}

ShellViewModel::ShellViewModel(ProjectService *projectService,
                               ImportService *importService,
                               FeedbackService *feedbackService,
                               StorageVolumeService *storageVolumeService,
                               QObject *parent)
    : QObject(parent)
    , m_projectService(projectService)
    , m_importService(importService)
    , m_feedbackService(feedbackService)
    , m_storageVolumeService(storageVolumeService)
{
    connect(m_projectService, &ProjectService::projectChanged, this, &ShellViewModel::stateChanged);
    connect(m_importService, &ImportService::importStateChanged, this, [this]() {
        m_lastMessage = m_importService->lastMessage();
        emit stateChanged();
    });
    connect(m_importService, &ImportService::catalogChanged, this, &ShellViewModel::stateChanged);
    connect(m_importService, &ImportService::sourceRootsChanged, this, &ShellViewModel::stateChanged);
    if (m_feedbackService) {
        connect(m_feedbackService, &FeedbackService::unreadCountChanged, this, &ShellViewModel::stateChanged);
    }
    if (m_storageVolumeService) {
        connect(m_storageVolumeService,
                &StorageVolumeService::volumesChanged,
                this,
                &ShellViewModel::storageVolumesChanged);
    }
}

QString ShellViewModel::projectName() const
{
    return m_projectService->hasOpenProject() ? m_projectService->currentProject().name : QStringLiteral("未打开项目");
}

QString ShellViewModel::projectPath() const
{
    return m_projectService->currentProject().rootPath;
}

bool ShellViewModel::projectEntered() const
{
    return m_projectEntered && m_projectService->hasOpenProject();
}

QString ShellViewModel::globalSearchText() const
{
    return m_globalSearchText;
}

int ShellViewModel::currentWorkspace() const
{
    return static_cast<int>(m_currentWorkspace);
}

QString ShellViewModel::statusSummary() const
{
    if (!m_projectService->hasOpenProject()) {
        return QStringLiteral("请先在项目库新建或打开项目");
    }
    if (!projectEntered()) {
        return QStringLiteral("请在项目库点击项目卡片进入项目");
    }
    return QStringLiteral("项目已就绪");
}

QString ShellViewModel::lastMessage() const
{
    return m_lastMessage;
}

int ShellViewModel::sourceRootCount() const
{
    return m_importService ? m_importService->sourceRoots().size() : 0;
}

bool ShellViewModel::sourceScanInProgress() const
{
    if (!m_importService) {
        return false;
    }
    for (const auto &sourceRoot : m_importService->sourceRoots()) {
        if (sourceRoot.status == QStringLiteral("scanning")) {
            return true;
        }
    }
    return false;
}

QVariantList ShellViewModel::workspaceTabs() const
{
    const auto projectReady = projectEntered();
    return {
        workspaceTab(QStringLiteral("项目库"), WorkspaceId::ProjectLibrary, 70, true),
        workspaceTab(QStringLiteral("素材库"), WorkspaceId::Library, 70, projectReady),
        workspaceTab(QStringLiteral("素材管理中心"), WorkspaceId::MaterialCenter, 108, projectReady),
        workspaceTab(QStringLiteral("报表"), WorkspaceId::Report, 56, projectReady),
        workspaceTab(QStringLiteral("任务"), WorkspaceId::Jobs, 56, projectReady),
        workspaceTab(QStringLiteral("使用反馈"), WorkspaceId::Feedback, 92, true, m_feedbackService ? m_feedbackService->unreadCount() : 0)
    };
}

int ShellViewModel::projectLibraryWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::ProjectLibrary);
}

int ShellViewModel::libraryWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Library);
}

int ShellViewModel::materialCenterWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::MaterialCenter);
}

int ShellViewModel::reportWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Report);
}

int ShellViewModel::jobsWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Jobs);
}

int ShellViewModel::feedbackWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Feedback);
}

QVariantList ShellViewModel::storageVolumes() const
{
    return m_storageVolumeService ? m_storageVolumeService->volumes() : QVariantList{};
}

void ShellViewModel::resetProjectUiState()
{
    if (!m_projectService->hasOpenProject()) {
        m_projectEntered = false;
        if (m_currentWorkspace != WorkspaceId::ProjectLibrary && m_currentWorkspace != WorkspaceId::Feedback) {
            m_currentWorkspace = WorkspaceId::ProjectLibrary;
            emit currentWorkspaceChanged();
        }
    }
    if (m_globalSearchText.isEmpty()) {
        return;
    }
    m_globalSearchText.clear();
    emit globalSearchTextChanged();
    emit searchRequested(m_globalSearchText);
}

void ShellViewModel::enterProjectFromLibrary()
{
    if (!m_projectService->hasOpenProject()) {
        m_projectEntered = false;
        m_lastMessage = QStringLiteral("请先在项目库打开一个项目。");
        emit stateChanged();
        return;
    }

    m_projectEntered = true;
    if (m_currentWorkspace != WorkspaceId::Library) {
        m_currentWorkspace = WorkspaceId::Library;
        emit currentWorkspaceChanged();
    }
    emit stateChanged();
    emit searchRequested(m_globalSearchText);
}

void ShellViewModel::enterMaterialCenterFromQuickSearch(const QString &searchText)
{
    Q_UNUSED(searchText);
    if (!m_projectService->hasOpenProject()) {
        m_projectEntered = false;
        m_lastMessage = QStringLiteral("无法定位快捷搜索结果：所属工程尚未打开。");
        if (m_currentWorkspace != WorkspaceId::ProjectLibrary) {
            m_currentWorkspace = WorkspaceId::ProjectLibrary;
            emit currentWorkspaceChanged();
        }
        emit stateChanged();
        return;
    }

    // Apply the navigation state without dispatching the quick-search query to
    // the main-window search field.
    m_projectEntered = true;
    // 快捷搜索拥有独立的输入语义。进入主窗口只切换工作区，不能把快捷
    // 查询写回主搜索框，也不能触发主搜索框对应的搜索链路。
    if (m_currentWorkspace != WorkspaceId::MaterialCenter) {
        m_currentWorkspace = WorkspaceId::MaterialCenter;
        emit currentWorkspaceChanged();
    }
    m_lastMessage = QStringLiteral("已进入素材管理中心并定位快捷搜索结果。");
    emit stateChanged();
}

void ShellViewModel::setGlobalSearchText(const QString &text)
{
    if (m_globalSearchText == text) {
        return;
    }
    m_globalSearchText = text;
    emit globalSearchTextChanged();
    emit searchRequested(text);
}

void ShellViewModel::setCurrentWorkspace(int workspace)
{
    const auto value = static_cast<WorkspaceId>(workspace);
    const auto normalizedValue = value == WorkspaceId::Qc
        ? WorkspaceId::Library
        : value;
    if (normalizedValue != WorkspaceId::ProjectLibrary
        && normalizedValue != WorkspaceId::Feedback
        && !projectEntered()) {
        m_lastMessage = QStringLiteral("请先在项目库点击项目卡片进入项目。");
        if (m_currentWorkspace != WorkspaceId::ProjectLibrary) {
            m_currentWorkspace = WorkspaceId::ProjectLibrary;
            emit currentWorkspaceChanged();
        }
        emit stateChanged();
        return;
    }
    if (m_currentWorkspace == normalizedValue) {
        return;
    }
    m_currentWorkspace = normalizedValue;
    emit currentWorkspaceChanged();
    emit searchRequested(m_globalSearchText);
}

void ShellViewModel::createProject()
{
    m_currentWorkspace = WorkspaceId::ProjectLibrary;
    m_lastMessage = QStringLiteral("请在项目库页面新建项目。");
    emit currentWorkspaceChanged();
    emit stateChanged();
}

void ShellViewModel::openProject()
{
    m_currentWorkspace = WorkspaceId::ProjectLibrary;
    m_lastMessage = QStringLiteral("请在项目库页面打开项目。");
    emit currentWorkspaceChanged();
    emit stateChanged();
}

void ShellViewModel::closeProject()
{
    m_projectEntered = false;
    m_projectService->closeProject();
    if (m_currentWorkspace != WorkspaceId::ProjectLibrary) {
        m_currentWorkspace = WorkspaceId::ProjectLibrary;
        emit currentWorkspaceChanged();
    }
    m_lastMessage = QStringLiteral("项目已关闭");
    emit stateChanged();
}

void ShellViewModel::addSourceDirectory()
{
    if (!projectEntered()) {
        m_lastMessage = QStringLiteral("请先在项目库点击项目卡片进入项目。");
        if (m_currentWorkspace != WorkspaceId::ProjectLibrary) {
            m_currentWorkspace = WorkspaceId::ProjectLibrary;
            emit currentWorkspaceChanged();
        }
        emit stateChanged();
        return;
    }

    emit addSourceDirectoryRequested();
}

bool ShellViewModel::importSourceDirectory(const QUrl &directoryUrl)
{
    const auto directory = directoryUrl.toLocalFile();
    if (directory.isEmpty()) {
        m_lastMessage = QStringLiteral("已取消添加素材源。");
        emit stateChanged();
        return false;
    }
    return importSourcePath(directory);
}

bool ShellViewModel::importSourcePath(const QString &directoryPath)
{
    const auto directory = directoryPath.trimmed();
    if (directory.isEmpty()) {
        m_lastMessage = QStringLiteral("请输入本地文件夹或网络共享路径。");
        emit stateChanged();
        return false;
    }

    QString errorMessage;
    if (!m_importService->importDirectory(directory, &errorMessage)) {
        m_lastMessage = errorMessage;
        showWarning(QStringLiteral("添加素材源失败"), m_lastMessage);
        emit stateChanged();
        return false;
    } else {
        m_lastMessage = m_importService->lastMessage();
        emit sourceImported();
    }
    emit stateChanged();
    return true;
}

bool ShellViewModel::importStorageVolume(const QString &rootPath)
{
    const auto imported = importSourcePath(rootPath);
    if (imported) {
        m_lastMessage = QStringLiteral("已开始建立磁盘卷全盘索引：%1").arg(QDir::toNativeSeparators(rootPath));
        emit stateChanged();
    }
    return imported;
}

void ShellViewModel::refreshStorageVolumes()
{
    if (m_storageVolumeService) {
        m_storageVolumeService->refresh();
    }
}

void ShellViewModel::cancelAddSourceDirectory()
{
    m_lastMessage = QStringLiteral("已取消添加素材源。");
    emit stateChanged();
}

void ShellViewModel::openSettings()
{
    emit openSettingsRequested();
}
