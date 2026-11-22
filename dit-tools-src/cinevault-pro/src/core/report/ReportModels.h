#pragma once

#include "domain/Enums.h"

#include <QDateTime>
#include <QString>
#include <QVector>

struct ReportProjectInfo {
    QString projectName;
    QString projectRoot;
    QString projectCreatedAt;
    QString shootTime;
    QString location;
    QString director;
    QString cinematographer;
    QString ditName;
    QString exportTime;
};

struct ReportSectionOptions {
    bool cover = true;
    bool summary = true;
    bool sourceOverview = true;
    bool formatDistribution = true;
    bool thumbnailIndex = true;
    bool videoMetadata = true;
    bool audioMetadata = true;
    bool folderTree = true;
};

struct ReportSourceSummary {
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
};

struct ReportStreamSummary {
    QString kind;
    QString codec;
    qint64 bitRate = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    int sampleRate = 0;
};

struct ReportAssetRow {
    qint64 id = 0;
    qint64 sourceRootId = 0;
    QString sourceName;
    QString name;
    QString extension;
    QString absolutePath;
    QString relativePath;
    QString parentPath;
    AssetType assetType = AssetType::Unknown;
    qint64 sizeBytes = 0;
    QString modifiedAt;
    QString thumbnailPath;
    QString container;
    qint64 durationMs = 0;
    qint64 bitRate = 0;
    ProbeStatus probeStatus = ProbeStatus::Pending;
    QString metadataError;
    QVector<ReportStreamSummary> streams;
};

struct ReportTreeLine {
    QString text;
    int depth = 0;
    bool folder = true;
};

struct ReportDocument {
    ReportProjectInfo project;
    ReportSectionOptions sections;
    QVector<ReportSourceSummary> sources;
    QVector<ReportAssetRow> assets;
    QVector<ReportTreeLine> treeLines;
    qint64 totalFiles = 0;
    qint64 totalFolders = 0;
    qint64 totalSizeBytes = 0;
    qint64 videoCount = 0;
    qint64 audioCount = 0;
    qint64 imageCount = 0;
    qint64 otherCount = 0;
    qint64 warningCount = 0;
    qint64 metadataFailedCount = 0;
    qint64 thumbnailMissingCount = 0;
};
