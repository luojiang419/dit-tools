#include "application/AnalysisRunStore.h"
#include "shared/VisualAnalysisMetadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
bool writeJson(const QString &path, const QJsonObject &json, QString *error)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) return true;
    if (error) *error = QStringLiteral("保存解析检查点失败：%1").arg(file.errorString());
    return false;
}

bool readJson(const QString &path, QJsonObject *json, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
        if (error) *error = QStringLiteral("无法读取解析检查点：%1").arg(path);
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("解析检查点损坏，请重新解析：%1").arg(path);
        return false;
    }
    *json = doc.object();
    return true;
}

QJsonObject encode(const VisualAnalysisPlan &value)
{
    QJsonObject json;
    json.insert(QStringLiteral("videoKey"), value.videoKey);
    json.insert(QStringLiteral("samplingPolicy"), value.samplingPolicy);
    json.insert(QStringLiteral("frameInterval"), value.frameInterval);
    json.insert(QStringLiteral("structuredProfileVersion"), value.structuredProfileVersion);
    json.insert(QStringLiteral("sourceFrameCount"), value.sourceFrameCount);
    json.insert(QStringLiteral("plannedFrameCount"), value.plannedFrameCount);
    json.insert(QStringLiteral("assetSizeBytes"), QString::number(value.assetSizeBytes));
    json.insert(QStringLiteral("assetModifiedAt"), value.assetModifiedAt);
    json.insert(QStringLiteral("createdAt"), value.createdAt);
    json.insert(QStringLiteral("updatedAt"), value.updatedAt);
    return json;
}

void decode(const QJsonObject &json, VisualAnalysisPlan *value)
{
    value->videoKey = json.value(QStringLiteral("videoKey")).toString();
    value->samplingPolicy = json.value(QStringLiteral("samplingPolicy")).toString();
    value->frameInterval = json.value(QStringLiteral("frameInterval")).toInt();
    value->structuredProfileVersion = json.value(QStringLiteral("structuredProfileVersion")).toInt();
    value->sourceFrameCount = json.value(QStringLiteral("sourceFrameCount")).toInt();
    value->plannedFrameCount = json.value(QStringLiteral("plannedFrameCount")).toInt();
    value->assetSizeBytes = json.value(QStringLiteral("assetSizeBytes")).toString().toLongLong();
    value->assetModifiedAt = json.value(QStringLiteral("assetModifiedAt")).toString();
    value->createdAt = json.value(QStringLiteral("createdAt")).toString();
    value->updatedAt = json.value(QStringLiteral("updatedAt")).toString();
}

QJsonObject encode(const FrameAnalysisRecord &value)
{
    QJsonObject json;
    json.insert(QStringLiteral("videoKey"), value.videoKey);
    json.insert(QStringLiteral("frameNumber"), value.frameNumber);
    json.insert(QStringLiteral("timestampMs"), QString::number(value.timestampMs));
    json.insert(QStringLiteral("imagePath"), value.imagePath);
    json.insert(QStringLiteral("caption"), value.caption);
    json.insert(QStringLiteral("tags"), QJsonArray::fromStringList(value.tags));
    json.insert(QStringLiteral("objects"), QJsonArray::fromStringList(value.objects));
    json.insert(QStringLiteral("actions"), value.actions);
    json.insert(QStringLiteral("setting"), value.setting);
    json.insert(QStringLiteral("ocrText"), value.ocrText);
    json.insert(QStringLiteral("ocrBlocks"), QJsonArray::fromStringList(value.ocrBlocks));
    json.insert(QStringLiteral("structuredProfileVersion"), value.structuredProfileVersion);
    json.insert(QStringLiteral("factsComplete"), value.factsComplete);
    json.insert(QStringLiteral("modelName"), value.modelName);
    json.insert(QStringLiteral("promptVersion"), value.promptVersion);
    json.insert(QStringLiteral("analyzedAt"), value.analyzedAt);
    json.insert(QStringLiteral("errorMessage"), value.errorMessage);
    json.insert(QStringLiteral("retryCount"), value.retryCount);
    json.insert(QStringLiteral("lastHttpStatus"), value.lastHttpStatus);
    json.insert(QStringLiteral("lastAttemptAt"), value.lastAttemptAt);
    json.insert(QStringLiteral("entities"), VisualAnalysisMetadata::entityFactsToJson(value.entities));
    json.insert(QStringLiteral("analysisState"), static_cast<int>(value.analysisState));
    return json;
}

void decode(const QJsonObject &json, FrameAnalysisRecord *value)
{
    value->videoKey = json.value(QStringLiteral("videoKey")).toString();
    value->frameNumber = json.value(QStringLiteral("frameNumber")).toInt();
    value->timestampMs = json.value(QStringLiteral("timestampMs")).toString().toLongLong();
    value->imagePath = json.value(QStringLiteral("imagePath")).toString();
    value->caption = json.value(QStringLiteral("caption")).toString();
    for (const auto &item : json.value(QStringLiteral("tags")).toArray()) value->tags.append(item.toString());
    for (const auto &item : json.value(QStringLiteral("objects")).toArray()) value->objects.append(item.toString());
    value->actions = json.value(QStringLiteral("actions")).toString();
    value->setting = json.value(QStringLiteral("setting")).toString();
    value->ocrText = json.value(QStringLiteral("ocrText")).toString();
    for (const auto &item : json.value(QStringLiteral("ocrBlocks")).toArray()) value->ocrBlocks.append(item.toString());
    value->structuredProfileVersion = json.value(QStringLiteral("structuredProfileVersion")).toInt();
    value->factsComplete = json.value(QStringLiteral("factsComplete")).toBool();
    value->modelName = json.value(QStringLiteral("modelName")).toString();
    value->promptVersion = json.value(QStringLiteral("promptVersion")).toString();
    value->analyzedAt = json.value(QStringLiteral("analyzedAt")).toString();
    value->errorMessage = json.value(QStringLiteral("errorMessage")).toString();
    value->retryCount = json.value(QStringLiteral("retryCount")).toInt();
    value->lastHttpStatus = json.value(QStringLiteral("lastHttpStatus")).toInt();
    value->lastAttemptAt = json.value(QStringLiteral("lastAttemptAt")).toString();
    value->entities = VisualAnalysisMetadata::entityFactsFromJson(json.value(QStringLiteral("entities")).toString());
    value->analysisState = static_cast<FrameAnalysisState>(json.value(QStringLiteral("analysisState")).toInt());
}

}

AnalysisRunStore::AnalysisRunStore(QString cacheDirectory)
    : m_cacheDirectory(QDir::cleanPath(std::move(cacheDirectory)))
{
}

bool AnalysisRunStore::begin(const VisualAnalysisPlan &plan,
                             const QVector<FrameAnalysisRecord> &frames,
                             const QString &model, QString *error)
{
    if (frames.isEmpty()) {
        if (error) *error = QStringLiteral("重建计划没有视频帧");
        return false;
    }
    m_runDirectory = QFileInfo(frames.first().imagePath).absolutePath();
    QJsonArray rows;
    for (const auto &frame : frames) rows.append(encode(frame));
    return writeJson(QDir(m_cacheDirectory).filePath(QStringLiteral("pending-run.json")),
                     {{QStringLiteral("version"), 1}, {QStringLiteral("model"), model},
                      {QStringLiteral("plan"), encode(plan)},
                      {QStringLiteral("directory"), m_runDirectory},
                      {QStringLiteral("frames"), rows}}, error);
}

bool AnalysisRunStore::load(const VisualAnalysisPlan &expected, const QString &model,
                            VisualAnalysisPlan *plan, QVector<FrameAnalysisRecord> *frames,
                            bool *found, QString *error)
{
    *found = false;
    const auto path = QDir(m_cacheDirectory).filePath(QStringLiteral("pending-run.json"));
    if (!QFileInfo::exists(path)) return true;
    QJsonObject json;
    if (!readJson(path, &json, error)) return false;
    VisualAnalysisPlan stored;
    decode(json.value(QStringLiteral("plan")).toObject(), &stored);
    if (json.value(QStringLiteral("version")).toInt() != 1
        || stored.videoKey != expected.videoKey || stored.samplingPolicy != expected.samplingPolicy
        || stored.assetSizeBytes != expected.assetSizeBytes || stored.assetModifiedAt != expected.assetModifiedAt
        || json.value(QStringLiteral("model")).toString() != model) return true;
    const auto directory = QDir::cleanPath(json.value(QStringLiteral("directory")).toString());
    const auto runsRoot = QDir(m_cacheDirectory).absoluteFilePath(QStringLiteral("runs"));
    if (!directory.startsWith(runsRoot + QLatin1Char('/')) || !QDir(directory).exists()) return true;
    QVector<FrameAnalysisRecord> restored;
    for (const auto &row : json.value(QStringLiteral("frames")).toArray()) {
        FrameAnalysisRecord frame;
        decode(row.toObject(), &frame);
        if (frame.frameNumber <= 0 || frame.videoKey != stored.videoKey
            || !QFileInfo::exists(frame.imagePath)) return true;
        const auto checkpoint = QDir(directory).filePath(QStringLiteral("analysis-%1.json").arg(frame.frameNumber));
        if (QFileInfo::exists(checkpoint)) {
            QJsonObject saved;
            if (!readJson(checkpoint, &saved, error)) return false;
            FrameAnalysisRecord analyzed;
            decode(saved, &analyzed);
            if (analyzed.videoKey != frame.videoKey || analyzed.frameNumber != frame.frameNumber
                || analyzed.imagePath != frame.imagePath || analyzed.timestampMs != frame.timestampMs) {
                if (error) *error = QStringLiteral("解析检查点与帧计划不一致");
                return false;
            }
            frame = std::move(analyzed);
        }
        restored.append(std::move(frame));
    }
    if (restored.isEmpty() || restored.size() != stored.plannedFrameCount) return true;
    m_runDirectory = directory;
    *plan = std::move(stored);
    *frames = std::move(restored);
    *found = true;
    return true;
}

bool AnalysisRunStore::saveFrame(const FrameAnalysisRecord &frame, QString *error) const
{
    if (m_runDirectory.isEmpty() || QFileInfo(frame.imagePath).absolutePath() != m_runDirectory) {
        if (error) *error = QStringLiteral("解析检查点没有有效的运行目录");
        return false;
    }
    return writeJson(QDir(m_runDirectory).filePath(QStringLiteral("analysis-%1.json").arg(frame.frameNumber)),
                     encode(frame), error);
}

void AnalysisRunStore::markPublished() const
{
    QFile::remove(QDir(m_cacheDirectory).filePath(QStringLiteral("pending-run.json")));
}

bool AnalysisRunStore::hasPendingRun() const
{
    return QFileInfo::exists(QDir(m_cacheDirectory).filePath(QStringLiteral("pending-run.json")));
}
