#include "ui/viewmodels/MinimalShellViewModel.h"

#include <QVariantMap>

MinimalShellViewModel::MinimalShellViewModel(QObject *parent)
    : QObject(parent)
{
}

QString MinimalShellViewModel::projectName() const
{
    return m_projectName;
}

QString MinimalShellViewModel::projectPath() const
{
    return m_projectPath;
}

bool MinimalShellViewModel::projectEntered() const
{
    return true;
}

QString MinimalShellViewModel::globalSearchText() const
{
    return m_globalSearchText;
}

int MinimalShellViewModel::currentWorkspace() const
{
    return static_cast<int>(m_currentWorkspace);
}

QString MinimalShellViewModel::statusSummary() const
{
    return QStringLiteral("窗口壳体可测试");
}

QString MinimalShellViewModel::lastMessage() const
{
    return m_lastMessage;
}

QVariantList MinimalShellViewModel::workspaceTabs() const
{
    return {
        QVariantMap{{QStringLiteral("label"), QStringLiteral("项目库")}, {QStringLiteral("value"), static_cast<int>(WorkspaceId::ProjectLibrary)}, {QStringLiteral("buttonWidth"), 70}, {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("素材库")}, {QStringLiteral("value"), static_cast<int>(WorkspaceId::Library)}, {QStringLiteral("buttonWidth"), 70}, {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("素材管理中心")}, {QStringLiteral("value"), static_cast<int>(WorkspaceId::MaterialCenter)}, {QStringLiteral("buttonWidth"), 108}, {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("报表")}, {QStringLiteral("value"), static_cast<int>(WorkspaceId::Report)}, {QStringLiteral("buttonWidth"), 56}, {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("任务")}, {QStringLiteral("value"), static_cast<int>(WorkspaceId::Jobs)}, {QStringLiteral("buttonWidth"), 56}, {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("使用反馈")}, {QStringLiteral("value"), static_cast<int>(WorkspaceId::Feedback)}, {QStringLiteral("buttonWidth"), 92}, {QStringLiteral("enabled"), true}}
    };
}

int MinimalShellViewModel::projectLibraryWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::ProjectLibrary);
}

int MinimalShellViewModel::libraryWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Library);
}

int MinimalShellViewModel::materialCenterWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::MaterialCenter);
}

int MinimalShellViewModel::reportWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Report);
}

int MinimalShellViewModel::jobsWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Jobs);
}

int MinimalShellViewModel::feedbackWorkspaceId() const
{
    return static_cast<int>(WorkspaceId::Feedback);
}

void MinimalShellViewModel::setGlobalSearchText(const QString &text)
{
    if (m_globalSearchText == text) {
        return;
    }
    m_globalSearchText = text;
    emit globalSearchTextChanged();
    emit searchRequested(text);
}

void MinimalShellViewModel::setCurrentWorkspace(int workspace)
{
    const auto value = static_cast<WorkspaceId>(workspace);
    const auto nextWorkspace = value == WorkspaceId::Qc
        ? WorkspaceId::Library
        : value;
    if (m_currentWorkspace == nextWorkspace) {
        return;
    }
    m_currentWorkspace = nextWorkspace;
    emit currentWorkspaceChanged();
    emit searchRequested(m_globalSearchText);
}

void MinimalShellViewModel::createProject()
{
    m_projectName = QStringLiteral("里程碑A-窗口壳体");
    m_projectPath = QStringLiteral("项目流程将在下一里程碑接回");
    m_lastMessage = QStringLiteral("当前为最小GUI测试模式，新建项目动作已占位。");
    emit stateChanged();
}

void MinimalShellViewModel::openProject()
{
    m_projectName = QStringLiteral("示例测试项目");
    m_projectPath = QStringLiteral("打开项目动作已占位，当前不落盘");
    m_lastMessage = QStringLiteral("当前为最小GUI测试模式，打开项目动作已占位。");
    emit stateChanged();
}

void MinimalShellViewModel::closeProject()
{
    m_projectName = QStringLiteral("最小GUI首测包");
    m_projectPath = QStringLiteral("当前为窗口壳体测试模式");
    m_lastMessage = QStringLiteral("已回到窗口壳体测试状态。");
    emit stateChanged();
}

void MinimalShellViewModel::addSourceDirectory()
{
    m_lastMessage = QStringLiteral("素材源导入将在后续里程碑接回，本轮仅测试界面壳体。");
    emit stateChanged();
}

void MinimalShellViewModel::openSettings()
{
    emit openSettingsRequested();
}
