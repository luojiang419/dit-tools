#include "ui/viewmodels/ReportWorkspaceViewModel.h"

#include "application/LibraryQueryService.h"
#include "application/ProjectService.h"
#include "application/ReportExportService.h"
#include "infrastructure/config/AppSettings.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>
#include <QtGlobal>

namespace {
QWidget *dialogParent()
{
    return QApplication::activeWindow();
}

void showInfo(const QString &title, const QString &message)
{
    QMessageBox::information(dialogParent(), title, message);
}

void showWarning(const QString &title, const QString &message)
{
    QMessageBox::warning(dialogParent(), title, message);
}
}

ReportWorkspaceViewModel::ReportWorkspaceViewModel(ProjectService *projectService,
                                                   LibraryQueryService *libraryQueryService,
                                                   ReportExportService *reportExportService,
                                                   AppSettings *settings,
                                                   QObject *parent)
    : QObject(parent)
    , m_projectService(projectService)
    , m_libraryQueryService(libraryQueryService)
    , m_reportExportService(reportExportService)
    , m_settings(settings)
    , m_statusText(QStringLiteral("请先在左侧选择素材目录。"))
{
    connect(m_projectService, &ProjectService::projectChanged, this, &ReportWorkspaceViewModel::resetForProject);
}

QString ReportWorkspaceViewModel::projectName() const
{
    return m_projectService->hasOpenProject() ? m_projectService->currentProject().name : QStringLiteral("未打开项目");
}

QString ReportWorkspaceViewModel::projectPath() const
{
    return m_projectService->currentProject().rootPath;
}

QString ReportWorkspaceViewModel::statusText() const
{
    if (!m_projectService->hasOpenProject()) {
        return QStringLiteral("请先创建或打开项目。");
    }
    if (!hasSelectedSource()) {
        return QStringLiteral("请先在左侧选择素材目录。");
    }
    return m_statusText;
}

QString ReportWorkspaceViewModel::lastExportPath() const
{
    return m_lastExportPath;
}

QString ReportWorkspaceViewModel::defaultShootTime() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

qint64 ReportWorkspaceViewModel::selectedSourceId() const
{
    return m_selectedSourceId;
}

bool ReportWorkspaceViewModel::hasSelectedSource() const
{
    return m_selectedSourceId > 0;
}

QString ReportWorkspaceViewModel::scopeText() const
{
    if (!m_projectService->hasOpenProject()) {
        return QStringLiteral("范围：请先创建或打开项目");
    }
    if (!hasSelectedSource()) {
        return QStringLiteral("范围：请先在左侧选择素材目录");
    }
    if (m_selectedSourceName.isEmpty()) {
        return m_selectedSourcePath.isEmpty()
            ? QStringLiteral("范围：当前选中的素材目录")
            : QStringLiteral("范围：%1").arg(m_selectedSourcePath);
    }
    if (m_selectedSourcePath.isEmpty()) {
        return QStringLiteral("范围：%1").arg(m_selectedSourceName);
    }
    return QStringLiteral("范围：%1 (%2)").arg(m_selectedSourceName, m_selectedSourcePath);
}

QUrl ReportWorkspaceViewModel::previewPageUrl() const
{
    if (m_previewPageIndex < 0 || m_previewPageIndex >= m_previewPagePaths.size()) {
        return {};
    }
    return QUrl::fromLocalFile(m_previewPagePaths.at(m_previewPageIndex));
}

int ReportWorkspaceViewModel::previewPageIndex() const
{
    return m_previewPageIndex;
}

int ReportWorkspaceViewModel::previewPageCount() const
{
    return m_previewPagePaths.size();
}

qreal ReportWorkspaceViewModel::previewZoom() const
{
    return m_previewZoom;
}

bool ReportWorkspaceViewModel::hasPreview() const
{
    return !m_previewPagePaths.isEmpty();
}

bool ReportWorkspaceViewModel::canPreview() const
{
    return m_projectService->hasOpenProject() && hasSelectedSource() && !m_isPreviewing && !m_isExporting;
}

bool ReportWorkspaceViewModel::canExport() const
{
    return m_projectService->hasOpenProject() && hasSelectedSource() && !m_isExporting && !m_isPreviewing;
}

bool ReportWorkspaceViewModel::isExporting() const
{
    return m_isExporting;
}

bool ReportWorkspaceViewModel::isPreviewing() const
{
    return m_isPreviewing;
}

void ReportWorkspaceViewModel::resetForProject()
{
    m_lastExportPath.clear();
    m_selectedSourceId = 0;
    m_selectedSourceName.clear();
    m_selectedSourcePath.clear();
    clearPreview();
    m_previewZoom = 0.75;
    m_statusText = m_projectService->hasOpenProject()
        ? QStringLiteral("请先在左侧选择素材目录。")
        : QStringLiteral("请先创建或打开项目。");
    emit stateChanged();
}

void ReportWorkspaceViewModel::setSelectedSource(qint64 sourceRootId)
{
    if (m_selectedSourceId == sourceRootId) {
        return;
    }

    m_selectedSourceId = sourceRootId;
    m_selectedSourceName.clear();
    m_selectedSourcePath.clear();
    updateSelectedSourceDetails();
    clearPreview();

    if (!m_projectService->hasOpenProject()) {
        m_statusText = QStringLiteral("请先创建或打开项目。");
    } else if (!hasSelectedSource()) {
        m_statusText = QStringLiteral("请先在左侧选择素材目录。");
    } else {
        m_statusText = QStringLiteral("已选择素材目录，准备生成该目录报表。");
    }

    emit stateChanged();
}

void ReportWorkspaceViewModel::exportPdf(const QString &shootTime,
                                         const QString &location,
                                         const QString &director,
                                         const QString &cinematographer,
                                         const QString &ditName)
{
    if (!m_projectService->hasOpenProject()) {
        m_statusText = QStringLiteral("请先创建或打开项目。");
        emit stateChanged();
        showWarning(QStringLiteral("导出 PDF 报表"), m_statusText);
        return;
    }
    if (!hasSelectedSource()) {
        m_statusText = QStringLiteral("请先在左侧选择素材目录。");
        emit stateChanged();
        showWarning(QStringLiteral("导出 PDF 报表"), m_statusText);
        return;
    }

    auto defaultPath = m_reportExportService->defaultReportPath();
    const auto selectedPath = QFileDialog::getSaveFileName(dialogParent(),
                                                           QStringLiteral("导出 PDF 报表"),
                                                           defaultPath,
                                                           QStringLiteral("PDF 报表 (*.pdf)"));
    if (selectedPath.isEmpty()) {
        m_statusText = QStringLiteral("已取消导出 PDF 报表。");
        emit stateChanged();
        return;
    }

    auto request = buildRequest(shootTime, location, director, cinematographer, ditName);
    request.outputPath = selectedPath;

    m_isExporting = true;
    m_statusText = QStringLiteral("正在生成 PDF 报表...");
    emit stateChanged();

    QString outputPath;
    QString errorMessage;
    const auto ok = m_reportExportService->exportPdf(request, &outputPath, &errorMessage);

    m_isExporting = false;
    if (!ok) {
        m_statusText = errorMessage.isEmpty() ? QStringLiteral("PDF 报表导出失败。") : errorMessage;
        emit stateChanged();
        showWarning(QStringLiteral("导出 PDF 报表失败"), m_statusText);
        return;
    }

    m_lastExportPath = outputPath;
    m_statusText = QStringLiteral("PDF 报表已导出：%1").arg(outputPath);
    emit stateChanged();
    showInfo(QStringLiteral("导出 PDF 报表"), m_statusText);
}

void ReportWorkspaceViewModel::refreshPreview(const QString &shootTime,
                                              const QString &location,
                                              const QString &director,
                                              const QString &cinematographer,
                                              const QString &ditName)
{
    if (!m_projectService->hasOpenProject()) {
        m_statusText = QStringLiteral("请先创建或打开项目。");
        emit stateChanged();
        return;
    }
    if (!hasSelectedSource()) {
        m_statusText = QStringLiteral("请先在左侧选择素材目录。");
        clearPreview();
        emit stateChanged();
        return;
    }

    const auto request = buildRequest(shootTime, location, director, cinematographer, ditName);

    m_isPreviewing = true;
    m_statusText = QStringLiteral("正在生成报表预览...");
    emit stateChanged();

    QStringList pagePaths;
    QString errorMessage;
    const auto ok = m_reportExportService->generatePreview(request, &pagePaths, &errorMessage);

    m_isPreviewing = false;
    if (!ok) {
        m_previewPagePaths.clear();
        m_previewPageIndex = 0;
        m_statusText = errorMessage.isEmpty() ? QStringLiteral("报表预览生成失败。") : errorMessage;
        emit stateChanged();
        return;
    }

    m_previewPagePaths = pagePaths;
    m_previewPageIndex = 0;
    if (m_previewZoom <= 0.0) {
        m_previewZoom = 0.75;
    }
    m_statusText = QStringLiteral("报表预览已生成，共 %1 页。").arg(m_previewPagePaths.size());
    emit stateChanged();
}

void ReportWorkspaceViewModel::nextPreviewPage()
{
    if (m_previewPageIndex + 1 >= m_previewPagePaths.size()) {
        return;
    }
    ++m_previewPageIndex;
    emit stateChanged();
}

void ReportWorkspaceViewModel::previousPreviewPage()
{
    if (m_previewPageIndex <= 0) {
        return;
    }
    --m_previewPageIndex;
    emit stateChanged();
}

void ReportWorkspaceViewModel::zoomPreviewIn()
{
    m_previewZoom = qMin<qreal>(2.5, m_previewZoom + 0.15);
    emit stateChanged();
}

void ReportWorkspaceViewModel::zoomPreviewOut()
{
    m_previewZoom = qMax<qreal>(0.45, m_previewZoom - 0.15);
    emit stateChanged();
}

void ReportWorkspaceViewModel::resetPreviewZoom()
{
    m_previewZoom = 0.75;
    emit stateChanged();
}

void ReportWorkspaceViewModel::clearPreview()
{
    m_previewPagePaths.clear();
    m_previewPageIndex = 0;
}

void ReportWorkspaceViewModel::updateSelectedSourceDetails()
{
    if (!m_libraryQueryService || !m_projectService->hasOpenProject() || !hasSelectedSource()) {
        return;
    }

    const auto sources = m_libraryQueryService->fetchSourceRoots();
    for (const auto &source : sources) {
        if (source.id != m_selectedSourceId) {
            continue;
        }
        m_selectedSourceName = source.name;
        m_selectedSourcePath = source.path;
        return;
    }
}

void ReportWorkspaceViewModel::openLastExportFolder()
{
    if (m_lastExportPath.isEmpty()) {
        return;
    }
    const QFileInfo info(m_lastExportPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

bool ReportWorkspaceViewModel::reportSectionEnabled(const QString &section) const
{
    return m_settings && m_settings->reportEnabledSections().contains(section);
}

void ReportWorkspaceViewModel::setReportSectionEnabled(const QString &section, bool enabled)
{
    if (!m_settings) {
        return;
    }

    auto sections = m_settings->reportEnabledSections();
    if (enabled) {
        if (!sections.contains(section)) {
            sections.append(section);
        }
    } else {
        sections.removeAll(section);
    }
    m_settings->setReportEnabledSections(sections);
    emit stateChanged();
}

ReportExportRequest ReportWorkspaceViewModel::buildRequest(const QString &shootTime,
                                                           const QString &location,
                                                           const QString &director,
                                                           const QString &cinematographer,
                                                           const QString &ditName) const
{
    ReportExportRequest request;
    request.sourceRootId = m_selectedSourceId;
    request.shootTime = shootTime;
    request.location = location;
    request.director = director;
    request.cinematographer = cinematographer;
    request.ditName = ditName;
    request.sections.cover = reportSectionEnabled(QStringLiteral("cover"));
    request.sections.summary = reportSectionEnabled(QStringLiteral("summary"));
    request.sections.sourceOverview = reportSectionEnabled(QStringLiteral("sourceOverview"));
    request.sections.formatDistribution = reportSectionEnabled(QStringLiteral("formatDistribution"));
    request.sections.thumbnailIndex = reportSectionEnabled(QStringLiteral("thumbnailIndex"));
    request.sections.videoMetadata = reportSectionEnabled(QStringLiteral("videoMetadata"));
    request.sections.audioMetadata = reportSectionEnabled(QStringLiteral("audioMetadata"));
    request.sections.folderTree = reportSectionEnabled(QStringLiteral("folderTree"));
    return request;
}
