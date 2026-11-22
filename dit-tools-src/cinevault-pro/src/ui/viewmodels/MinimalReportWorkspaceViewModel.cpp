#include "ui/viewmodels/MinimalReportWorkspaceViewModel.h"

#include <QDateTime>

MinimalReportWorkspaceViewModel::MinimalReportWorkspaceViewModel(QObject *parent)
    : QObject(parent)
    , m_statusText(QStringLiteral("最小界面暂未接入真实项目报表导出。"))
{
}

QString MinimalReportWorkspaceViewModel::projectName() const
{
    return QStringLiteral("演示模式");
}

QString MinimalReportWorkspaceViewModel::projectPath() const
{
    return {};
}

QString MinimalReportWorkspaceViewModel::statusText() const
{
    return m_statusText;
}

QString MinimalReportWorkspaceViewModel::lastExportPath() const
{
    return {};
}

QString MinimalReportWorkspaceViewModel::defaultShootTime() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

qint64 MinimalReportWorkspaceViewModel::selectedSourceId() const
{
    return m_selectedSourceId;
}

bool MinimalReportWorkspaceViewModel::hasSelectedSource() const
{
    return m_selectedSourceId > 0;
}

QString MinimalReportWorkspaceViewModel::scopeText() const
{
    return hasSelectedSource()
        ? QStringLiteral("范围：当前选中的素材目录")
        : QStringLiteral("范围：请先在左侧选择素材目录");
}

QUrl MinimalReportWorkspaceViewModel::previewPageUrl() const
{
    return {};
}

int MinimalReportWorkspaceViewModel::previewPageIndex() const
{
    return 0;
}

int MinimalReportWorkspaceViewModel::previewPageCount() const
{
    return 0;
}

qreal MinimalReportWorkspaceViewModel::previewZoom() const
{
    return 0.75;
}

bool MinimalReportWorkspaceViewModel::hasPreview() const
{
    return false;
}

bool MinimalReportWorkspaceViewModel::canPreview() const
{
    return false;
}

bool MinimalReportWorkspaceViewModel::canExport() const
{
    return false;
}

bool MinimalReportWorkspaceViewModel::isExporting() const
{
    return false;
}

bool MinimalReportWorkspaceViewModel::isPreviewing() const
{
    return false;
}

void MinimalReportWorkspaceViewModel::setSelectedSource(qint64 sourceRootId)
{
    if (m_selectedSourceId == sourceRootId) {
        return;
    }
    m_selectedSourceId = sourceRootId;
    m_statusText = hasSelectedSource()
        ? QStringLiteral("演示模式：已选择素材目录。")
        : QStringLiteral("请先在左侧选择素材目录。");
    emit stateChanged();
}

void MinimalReportWorkspaceViewModel::exportPdf(const QString &shootTime,
                                                const QString &location,
                                                const QString &director,
                                                const QString &cinematographer,
                                                const QString &ditName)
{
    Q_UNUSED(shootTime);
    Q_UNUSED(location);
    Q_UNUSED(director);
    Q_UNUSED(cinematographer);
    Q_UNUSED(ditName);
    m_statusText = QStringLiteral("请使用真实工作流构建导出 PDF 报表。");
    emit stateChanged();
}

void MinimalReportWorkspaceViewModel::refreshPreview(const QString &shootTime,
                                                     const QString &location,
                                                     const QString &director,
                                                     const QString &cinematographer,
                                                     const QString &ditName)
{
    Q_UNUSED(shootTime);
    Q_UNUSED(location);
    Q_UNUSED(director);
    Q_UNUSED(cinematographer);
    Q_UNUSED(ditName);
    m_statusText = QStringLiteral("请使用真实工作流构建预览 PDF 报表。");
    emit stateChanged();
}

void MinimalReportWorkspaceViewModel::nextPreviewPage()
{
}

void MinimalReportWorkspaceViewModel::previousPreviewPage()
{
}

void MinimalReportWorkspaceViewModel::zoomPreviewIn()
{
}

void MinimalReportWorkspaceViewModel::zoomPreviewOut()
{
}

void MinimalReportWorkspaceViewModel::resetPreviewZoom()
{
}

void MinimalReportWorkspaceViewModel::openLastExportFolder()
{
}
