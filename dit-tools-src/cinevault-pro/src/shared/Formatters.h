#pragma once

#include "domain/Entities.h"

#include <QString>

class Formatters {
public:
    static QString formatBytes(qint64 bytes);
    static QString formatDuration(qint64 durationMs);
    static QString formatBitRate(qint64 bitRate);
    static QString assetTypeLabel(AssetType type);
    static QString statusLabel(const QString &status);
    static QString statusColor(const QString &status);
    static QString jobTypeLabel(JobType type);
    static QString jobStateLabel(JobState state);
    static QString jobProgressLabel(const JobProgressContext &context);
    static QString jobProgressShortLabel(const JobProgressContext &context);
    static QString workspaceLabel(WorkspaceId workspaceId);
    static QString probeStatusLabel(ProbeStatus status);
    static QString analysisModeLabel(AnalysisMode mode);
    static QString videoAnalysisStatusLabel(VideoAnalysisStatus status, ConfirmationStatus confirmationStatus);
    static QString confirmationStatusLabel(ConfirmationStatus status);
};
