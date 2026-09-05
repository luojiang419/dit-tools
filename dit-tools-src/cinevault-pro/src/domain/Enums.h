#pragma once

#include <QtGlobal>

enum class AssetType : qint32 {
    Unknown = 0,
    Video,
    Audio,
    Image,
    Subtitle,
    ProjectFile,
    Document,
    Archive,
    Other
};

enum class JobType : qint32 {
    Scan = 0,
    Metadata,
    Thumbnail,
    GlobalSync,
    ContentAnalysis,
    Preview,
    Report,
    Export
};

enum class JobState : qint32 {
    Pending = 0,
    Running,
    Completed,
    Failed,
    Cancelled
};

enum class WorkspaceId : qint32 {
    ProjectLibrary = 0,
    Library = 2,
    MaterialCenter = 3,
    Qc = 4,
    Report = 5,
    Jobs = 6,
    Feedback = 7
};

enum class SelectionKind : qint32 {
    None = 0,
    SourceRoot,
    Folder,
    Asset
};

enum class ProbeStatus : qint32 {
    Pending = 0,
    Success,
    Unsupported,
    Unavailable,
    Failed
};

enum class MediaType : qint32 {
    Unknown = 0,
    Video,
    Audio,
    Image
};

enum class ThumbnailStatus : qint32 {
    Pending = 0,
    Success = 1,
    Running = 2,
    Failed = 4
};

enum class AnalysisMode : qint32 {
    Every10Frames = 0,
    EveryFrame = 1,
    CustomInterval = 2
};

// 与 filmstoryboard 保持一致的候选帧提取策略。AnalysisMode 仅保留用于
// 读取旧版本设置，素材中心的新解析不再使用它。
enum class VideoFrameExtractionStrategy : qint32 {
    PerFrame = 0,
    SceneAndInterval = 1,
    IntervalOnly = 2,
    HighFidelity = 3
};

enum class VideoAnalysisStatus : qint32 {
    Pending = 0,
    Running,
    Ready,
    Failed,
    IndexedOnly
};

enum class VideoAnalysisTaskStage : qint32 {
    Pending = 0,
    ExtractingFrames,
    AnalyzingFrames,
    Summarizing,
    Completed
};

enum class FrameAnalysisState : qint32 {
    Pending = 0,
    Success,
    Failed,
    Skipped
};

enum class AnalysisRunMode : qint32 {
    Initial = 0,
    Resume,
    Rebuild,
    SingleFrame
};

enum class ConfirmationStatus : qint32 {
    Pending = 0,
    Confirmed
};
