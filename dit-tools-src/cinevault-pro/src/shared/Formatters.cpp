#include "shared/Formatters.h"

#include <QStringList>

QString Formatters::formatBytes(qint64 bytes)
{
    static const QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")};
    double value = static_cast<double>(bytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < units.size() - 1) {
        value /= 1024.0;
        ++unitIndex;
    }
    return QString::number(value, unitIndex == 0 ? 'f' : 'f', unitIndex == 0 ? 0 : 1) + units.at(unitIndex);
}

QString Formatters::formatDuration(qint64 durationMs)
{
    if (durationMs <= 0) {
        return QStringLiteral("未知时长");
    }

    const qint64 totalSeconds = durationMs / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString Formatters::formatBitRate(qint64 bitRate)
{
    if (bitRate <= 0) {
        return QStringLiteral("未知码率");
    }

    if (bitRate >= 1000000) {
        return QString::number(static_cast<double>(bitRate) / 1000000.0, 'f', 1) + QStringLiteral(" Mbps");
    }
    if (bitRate >= 1000) {
        return QString::number(static_cast<double>(bitRate) / 1000.0, 'f', 0) + QStringLiteral(" kbps");
    }
    return QString::number(bitRate) + QStringLiteral(" bps");
}

QString Formatters::assetTypeLabel(AssetType type)
{
    switch (type) {
    case AssetType::Video: return QStringLiteral("视频");
    case AssetType::Audio: return QStringLiteral("音频");
    case AssetType::Image: return QStringLiteral("图片");
    case AssetType::Subtitle: return QStringLiteral("字幕");
    case AssetType::ProjectFile: return QStringLiteral("工程");
    case AssetType::Document: return QStringLiteral("文档");
    case AssetType::Archive: return QStringLiteral("压缩包");
    case AssetType::Other: return QStringLiteral("其他");
    case AssetType::Unknown:
    default: return QStringLiteral("未知");
    }
}

QString Formatters::statusLabel(const QString &status)
{
    if (status == QStringLiteral("scanning")) return QStringLiteral("正在扫描");
    if (status == QStringLiteral("ok")) return QStringLiteral("正常");
    if (status == QStringLiteral("warning")) return QStringLiteral("有警告");
    if (status == QStringLiteral("failed")) return QStringLiteral("失败");
    if (status == QStringLiteral("offline")) return QStringLiteral("离线");
    return QStringLiteral("待处理");
}

QString Formatters::statusColor(const QString &status)
{
    if (status == QStringLiteral("scanning")) return QStringLiteral("#4F8CFF");
    if (status == QStringLiteral("ok")) return QStringLiteral("#22C55E");
    if (status == QStringLiteral("warning")) return QStringLiteral("#F59E0B");
    if (status == QStringLiteral("failed")) return QStringLiteral("#EF4444");
    if (status == QStringLiteral("offline")) return QStringLiteral("#626D7D");
    return QStringLiteral("#9AA4B2");
}

QString Formatters::jobTypeLabel(JobType type)
{
    switch (type) {
    case JobType::Scan: return QStringLiteral("扫描");
    case JobType::Metadata: return QStringLiteral("元数据");
    case JobType::Thumbnail: return QStringLiteral("缩略图");
    case JobType::GlobalSync: return QStringLiteral("全局索引");
    case JobType::ContentAnalysis: return QStringLiteral("内容解析");
    case JobType::Preview: return QStringLiteral("预览");
    case JobType::Report: return QStringLiteral("报表");
    case JobType::Export: return QStringLiteral("导出");
    case JobType::Backup: return QStringLiteral("备份");
    }
    return QStringLiteral("任务");
}

QString Formatters::jobStateLabel(JobState state)
{
    switch (state) {
    case JobState::Pending: return QStringLiteral("等待中");
    case JobState::Running: return QStringLiteral("进行中");
    case JobState::Completed: return QStringLiteral("已完成");
    case JobState::Failed: return QStringLiteral("失败");
    case JobState::Cancelled: return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}

QString Formatters::jobProgressLabel(const JobProgressContext &context)
{
    QStringList parts;
    if (context.totalSteps > 0 && context.currentStep > 0) {
        QString stepText = QStringLiteral("第%1/%2步").arg(context.currentStep).arg(context.totalSteps);
        if (!context.stepLabel.trimmed().isEmpty()) {
            stepText += QStringLiteral(" · %1").arg(context.stepLabel.trimmed());
        }
        parts.append(stepText);
    } else if (!context.stepLabel.trimmed().isEmpty()) {
        parts.append(context.stepLabel.trimmed());
    }

    const auto unit = context.unitLabel.trimmed();
    if (context.totalItems > 0) {
        if (unit == QStringLiteral("帧")) {
            parts.append(QStringLiteral("第%1/%2帧").arg(context.currentItem).arg(context.totalItems));
        } else if (unit == QStringLiteral("字节")) {
            parts.append(QStringLiteral("%1/%2").arg(formatBytes(context.currentItem), formatBytes(context.totalItems)));
        } else if (!unit.isEmpty()) {
            parts.append(QStringLiteral("%1/%2%3").arg(context.currentItem).arg(context.totalItems).arg(unit));
        } else {
            parts.append(QStringLiteral("%1/%2").arg(context.currentItem).arg(context.totalItems));
        }
    } else if (context.currentItem > 0 && !unit.isEmpty()) {
        parts.append(QStringLiteral("%1%2").arg(context.currentItem).arg(unit));
    }

    if (context.currentFrameNumber > 0) {
        parts.append(QStringLiteral("当前帧 %1").arg(context.currentFrameNumber));
    }
    if (!context.extraLabel.trimmed().isEmpty()) {
        parts.append(context.extraLabel.trimmed());
    }

    return parts.join(QStringLiteral(" · "));
}

QString Formatters::jobProgressShortLabel(const JobProgressContext &context)
{
    QStringList parts;
    if (context.totalSteps > 0 && context.currentStep > 0) {
        parts.append(QStringLiteral("第%1/%2步").arg(context.currentStep).arg(context.totalSteps));
    }
    const auto unit = context.unitLabel.trimmed();
    if (context.totalItems > 0) {
        if (unit == QStringLiteral("帧")) {
            parts.append(QStringLiteral("第%1/%2帧").arg(context.currentItem).arg(context.totalItems));
        } else if (unit == QStringLiteral("字节")) {
            parts.append(QStringLiteral("%1/%2").arg(formatBytes(context.currentItem), formatBytes(context.totalItems)));
        } else if (!unit.isEmpty()) {
            parts.append(QStringLiteral("%1/%2%3").arg(context.currentItem).arg(context.totalItems).arg(unit));
        } else {
            parts.append(QStringLiteral("%1/%2").arg(context.currentItem).arg(context.totalItems));
        }
    } else if (!context.stepLabel.trimmed().isEmpty()) {
        parts.append(context.stepLabel.trimmed());
    }
    if (context.currentFrameNumber > 0) {
        parts.append(QStringLiteral("帧 %1").arg(context.currentFrameNumber));
    }
    return parts.join(QStringLiteral(" · "));
}

QString Formatters::workspaceLabel(WorkspaceId workspaceId)
{
    switch (workspaceId) {
    case WorkspaceId::ProjectLibrary: return QStringLiteral("项目库");
    case WorkspaceId::Import: return QStringLiteral("素材备份");
    case WorkspaceId::Library: return QStringLiteral("素材库");
    case WorkspaceId::MaterialCenter: return QStringLiteral("素材管理中心");
    case WorkspaceId::Qc: return QStringLiteral("素材库");
    case WorkspaceId::Report: return QStringLiteral("报表");
    case WorkspaceId::Jobs: return QStringLiteral("任务");
    case WorkspaceId::Feedback: return QStringLiteral("使用反馈");
    }
    return QStringLiteral("导入");
}

QString Formatters::probeStatusLabel(ProbeStatus status)
{
    switch (status) {
    case ProbeStatus::Pending: return QStringLiteral("等待中");
    case ProbeStatus::Success: return QStringLiteral("已完成");
    case ProbeStatus::Unsupported: return QStringLiteral("格式暂不支持");
    case ProbeStatus::Unavailable: return QStringLiteral("未启用 FFmpeg");
    case ProbeStatus::Failed: return QStringLiteral("执行失败");
    }
    return QStringLiteral("未知");
}

QString Formatters::analysisModeLabel(AnalysisMode mode)
{
    switch (mode) {
    case AnalysisMode::Every10Frames:
        return QStringLiteral("每10帧抽1帧");
    case AnalysisMode::EveryFrame:
        return QStringLiteral("逐帧解析");
    case AnalysisMode::CustomInterval:
    default:
        return QStringLiteral("自定义抽帧间隔");
    }
}

QString Formatters::videoAnalysisStatusLabel(VideoAnalysisStatus status, ConfirmationStatus confirmationStatus)
{
    switch (status) {
    case VideoAnalysisStatus::Pending:
        return QStringLiteral("待解析");
    case VideoAnalysisStatus::Running:
        return QStringLiteral("解析中");
    case VideoAnalysisStatus::Ready:
        return confirmationStatus == ConfirmationStatus::Confirmed
            ? QStringLiteral("已确认")
            : QStringLiteral("待确认");
    case VideoAnalysisStatus::Failed:
        return QStringLiteral("解析失败");
    case VideoAnalysisStatus::IndexedOnly:
        return QStringLiteral("仅索引");
    }
    return QStringLiteral("未知");
}

QString Formatters::confirmationStatusLabel(ConfirmationStatus status)
{
    switch (status) {
    case ConfirmationStatus::Confirmed:
        return QStringLiteral("已确认");
    case ConfirmationStatus::Pending:
    default:
        return QStringLiteral("未确认");
    }
}
