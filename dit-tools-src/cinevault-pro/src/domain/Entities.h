#pragma once

#include "domain/Enums.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <stop_token>
#include <QStringList>
#include <QVariantList>
#include <QVector>

struct Project {
    QString id;
    QString name;
    QString rootPath;
    QString databasePath;
    QString createdAt;
};

struct ProjectLibraryEntry {
    QString name;
    QString rootPath;
    QString databasePath;
    QString createdAt;
    bool available = false;
    bool current = false;
};

struct SourceRoot {
    qint64 id = 0;
    QString name;
    QString path;
    QString status;
    qint64 totalFiles = 0;
    qint64 totalFolders = 0;
    qint64 totalSizeBytes = 0;
    qint64 videoCount = 0;
    qint64 audioCount = 0;
    qint64 imageCount = 0;
    qint64 otherCount = 0;
    qint64 warningCount = 0;
    int scanVersion = 0;
};

struct FolderNode {
    qint64 id = 0;
    qint64 sourceRootId = 0;
    QString name;
    QString absolutePath;
    QString pathKey;
    QString relativePath;
    QString parentRelativePath;
    int depth = 0;
    qint64 fileCount = 0;
    qint64 directFileCount = 0;
    qint64 recursiveFileCount = 0;
    QString normalizedDate;
    QString dateAnchor;
};

struct AssetFile {
    qint64 id = 0;
    qint64 sourceRootId = 0;
    QString name;
    QString extension;
    QString absolutePath;
    QString relativePath;
    QString parentPath;
    AssetType assetType = AssetType::Unknown;
    qint64 sizeBytes = 0;
    QString modifiedAt;
    bool readable = false;
    QString thumbnailPath;
    ThumbnailStatus thumbnailStatus = ThumbnailStatus::Pending;
    QString container;
    qint64 durationMs = 0;
    qint64 bitRate = 0;
    ProbeStatus probeStatus = ProbeStatus::Pending;
    QString technicalSummary;
    bool favorite = false;
};

struct FormatInfo {
    QString container;
    qint64 durationMs = 0;
    qint64 bitRate = 0;
};

struct StreamInfo {
    int index = -1;
    QString codec;
    QString kind;
    qint64 bitRate = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    int sampleRate = 0;
};

struct MediaProbeResult {
    qint64 assetId = 0;
    ProbeStatus status = ProbeStatus::Pending;
    MediaType mediaType = MediaType::Unknown;
    FormatInfo format;
    QList<StreamInfo> streams;
    QString rawJson;
    QString errorMessage;
};

struct ThumbnailRequest {
    qint64 assetId = 0;
    QString sourcePath;
    QString cachePath;
    AssetType assetType = AssetType::Unknown;
    int frameIndex = 3;
    int maxWidth = 480;
    int maxHeight = 480;
};

struct ThumbnailResult {
    qint64 assetId = 0;
    bool success = false;
    bool retryable = false;
    QString outputPath;
    QString errorMessage;
};

struct ExtractedFrame {
    int frameNumber = 0;
    qint64 timestampMs = 0;
    QString imagePath;
};

struct FrameExtractionRequest {
    qint64 assetId = 0;
    QString sourcePath;
    QString outputDirectory;
    AnalysisMode mode = AnalysisMode::Every10Frames;
    int frameInterval = 10;
    int contactSheetFrameCount = 0;
    int maxWidth = 1920;
    int maxHeight = 1080;
    QVector<int> requestedFrameNumbers;
    bool preserveExistingFrames = false;
    VideoFrameExtractionStrategy strategy = VideoFrameExtractionStrategy::SceneAndInterval;
    double intervalSeconds = 1.0;
    double sceneThreshold = 0.3;
    double minimumSharpness = 0.01;
    std::stop_token stopToken;
};

struct FrameExtractionResult {
    qint64 assetId = 0;
    bool success = false;
    int sourceFrameCount = 0;
    int frameInterval = 1;
    QVector<ExtractedFrame> frames;
    QString errorMessage;
};

struct VisionEntityFact {
    QString category;
    QString label;
    QStringList colors;
    QStringList materials;
    QStringList attributes;
};

struct VisionFrameAnalysis {
    QString caption;
    QStringList tags;
    QStringList objects;
    QString actions;
    QString setting;
    QVector<VisionEntityFact> entities;
    QString ocrText;
    QStringList ocrBlocks;
    int structuredProfileVersion = 1;
    bool factsComplete = false;
};

struct VisionVideoSummary {
    QString summary;
    QStringList keywords;
    QStringList scenes;
};

struct MaterialDimensionAnalysis {
    QString name;
    QString detail;
    QString analyzedAt;
};

struct VideoAnalysisTask {
    QString videoKey;
    VideoAnalysisTaskStage stage = VideoAnalysisTaskStage::Pending;
    int totalFrames = 0;
    int completedFrames = 0;
    int successfulFrames = 0;
    int skippedFrames = 0;
    int summaryRetryCount = 0;
    QString lastErrorMessage;
    QString lastUpdatedAt;
};

struct AnalysisJob {
    QString videoKey;
    AnalysisRunMode mode = AnalysisRunMode::Initial;
    int frameNumber = 0;
};

struct GlobalVideoAsset {
    QString videoKey;
    QString assetKey;
    QString projectUuid;
    QString projectName;
    QString projectDatabasePath;
    qint64 sourceRootId = 0;
    QString sourceRootName;
    QString folderKey;
    bool available = true;
    qint64 assetId = 0;
    QString fileName;
    QString extension;
    QString absolutePath;
    QString relativePath;
    AssetType assetType = AssetType::Video;
    qint64 sizeBytes = 0;
    QString modifiedAt;
    QString captureTime;
    QString captureDate;
    QString captureTimeSource;
    double captureTimeConfidence = 0.0;
    qint64 durationMs = 0;
    QString thumbnailPath;
    ThumbnailStatus thumbnailStatus = ThumbnailStatus::Pending;
    VideoAnalysisStatus analysisStatus = VideoAnalysisStatus::Pending;
    ConfirmationStatus confirmationStatus = ConfirmationStatus::Pending;
    QString summary;
    QStringList keywords;
    QStringList scenes;
    QString searchText;
    QString technicalSummary;
    QString embeddedMetadataText;
    QString sourceText;
    QString errorMessage;
    QString updatedAt;
    QString analyzedAt;
    QString confirmedAt;
    double searchScore = 0.0;
    double searchConfidence = 0.0;
    QStringList searchReasons;
    int matchedFrameNumber = -1;
    qint64 matchedTimestampMs = -1;
    QString matchedFrameCaption;
    VideoAnalysisTask analysisTask;
};

struct FrameAnalysisRecord {
    qint64 id = 0;
    QString videoKey;
    int frameNumber = 0;
    qint64 timestampMs = 0;
    QString imagePath;
    QString caption;
    QStringList tags;
    QStringList objects;
    QString actions;
    QString setting;
    QVector<VisionEntityFact> entities;
    QString ocrText;
    QStringList ocrBlocks;
    int structuredProfileVersion = 1;
    bool factsComplete = false;
    QString modelName;
    QString promptVersion;
    QString analyzedAt;
    QString errorMessage;
    FrameAnalysisState analysisState = FrameAnalysisState::Pending;
    int retryCount = 0;
    int lastHttpStatus = 0;
    QString lastAttemptAt;
};

struct VisualAnalysisPlan {
    QString videoKey;
    QString samplingPolicy;
    int frameInterval = 1;
    int structuredProfileVersion = 1;
    int sourceFrameCount = 0;
    int plannedFrameCount = 0;
    qint64 assetSizeBytes = 0;
    QString assetModifiedAt;
    QString createdAt;
    QString updatedAt;
};

struct FeedbackAttachment {
    QString id;
    QString name;
    QString mimeType;
    QString url;
    qint64 sizeBytes = 0;
};

struct FeedbackMessage {
    qint64 id = 0;
    QString conversationId;
    QString senderRole;
    QString text;
    QVector<FeedbackAttachment> attachments;
    QString createdAt;
};

struct FeedbackConversation {
    QString conversationId;
    QString clientId;
    QString clientToken;
    QString clientWsUrl;
    QString nickname;
    QString contact;
    QString status;
    QString appVersion;
    QString systemSummary;
    QString projectName;
    QString projectPath;
    QString latestPreview;
    QString latestMessageAt;
    QString createdAt;
    QString updatedAt;
    int unreadAdmin = 0;
    int unreadClient = 0;
};

struct VideoAnalysisDetail {
    GlobalVideoAsset asset;
    VisualAnalysisPlan visualAnalysisPlan;
    bool hasVisualAnalysisPlan = false;
    QVector<FrameAnalysisRecord> frames;
    QVector<MaterialDimensionAnalysis> dimensionAnalyses;
};

struct VideoAnalysisDetailPage {
    VideoAnalysisDetail detail;
    int totalFrameCount = 0;
    int nextFrameNumber = 0;
    bool hasMoreFrames = false;
};

struct JobSubject {
    QString kind;
    QString key;
    QString name;
    QString path;
    QString thumbnailPath;
    ThumbnailStatus thumbnailStatus = ThumbnailStatus::Pending;
    QString typeLabel;
};

struct JobProgressContext {
    int currentStep = 0;
    int totalSteps = 0;
    QString stepLabel;
    qint64 currentItem = 0;
    qint64 totalItems = 0;
    QString unitLabel;
    int currentFrameNumber = 0;
    QString extraLabel;
};

struct Job {
    qint64 id = 0;
    JobType type = JobType::Scan;
    JobState state = JobState::Pending;
    QString title;
    QString detail;
    QString errorMessage;
    qint64 progress = 0;
    qint64 sourceRootId = 0;
    JobSubject subject;
    JobProgressContext progressContext;
    QDateTime startedAt;
    QDateTime updatedAt;
};

struct SelectionState {
    SelectionKind kind = SelectionKind::None;
    qint64 sourceRootId = 0;
    qint64 folderId = 0;
    qint64 assetId = 0;
};

struct ScanBatch {
    qint64 sourceRootId = 0;
    qint64 processedEntries = 0;
    qint64 totalFiles = 0;
    qint64 totalFolders = 0;
    qint64 totalSizeBytes = 0;
    qint64 warningCount = 0;
    qint64 progressPercent = 0;
};

struct InspectorState {
    QString title;
    QString subtitle;
    QVariantList details;
};
