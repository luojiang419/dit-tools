#include "shared/VisualAnalysisMetadata.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {
QStringList normalizedList(const QStringList &values)
{
    QStringList result;
    QSet<QString> seen;
    for (const auto &value : values) {
        const auto item = value.simplified();
        const auto key = item.toCaseFolded();
        if (item.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.append(item);
    }
    return result;
}
QJsonArray stringArray(const QStringList &values)
{
    QJsonArray array;
    for (const auto &value : normalizedList(values)) {
        array.append(value);
    }
    return array;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList values;
    if (value.isArray()) {
        for (const auto &entry : value.toArray()) {
            if (entry.isString()) {
                values.append(entry.toString());
            }
        }
    } else if (value.isString()) {
        values.append(value.toString());
    }
    return normalizedList(values);
}

void appendTerms(QStringList *terms, QSet<QString> *seen, const QStringList &values)
{
    for (const auto &value : normalizedList(values)) {
        const auto key = value.toCaseFolded();
        if (seen->contains(key)) {
            continue;
        }
        seen->insert(key);
        terms->append(value);
    }
}
}

QString VisualAnalysisMetadata::entityFactsToJson(const QVector<VisionEntityFact> &facts)
{
    QJsonArray array;
    for (const auto &fact : facts) {
        const auto label = fact.label.simplified();
        if (label.isEmpty()) {
            continue;
        }
        array.append(QJsonObject{
            {QStringLiteral("category"), fact.category.simplified()},
            {QStringLiteral("label"), label},
            {QStringLiteral("colors"), stringArray(fact.colors)},
            {QStringLiteral("materials"), stringArray(fact.materials)},
            {QStringLiteral("attributes"), stringArray(fact.attributes)}
        });
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<VisionEntityFact> VisualAnalysisMetadata::entityFactsFromJson(const QString &json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return {};
    }

    QVector<VisionEntityFact> facts;
    for (const auto &value : document.array()) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        VisionEntityFact fact;
        fact.category = object.value(QStringLiteral("category")).toString().simplified();
        fact.label = object.value(QStringLiteral("label")).toString().simplified();
        fact.colors = stringList(object.value(QStringLiteral("colors")));
        fact.materials = stringList(object.value(QStringLiteral("materials")));
        fact.attributes = stringList(object.value(QStringLiteral("attributes")));
        if (!fact.label.isEmpty()) {
            facts.append(fact);
        }
    }
    return facts;
}

QStringList VisualAnalysisMetadata::entityFactSearchTerms(const QVector<VisionEntityFact> &facts)
{
    QStringList terms;
    QSet<QString> seen;
    for (const auto &fact : facts) {
        appendTerms(&terms, &seen, {fact.category, fact.label});
        appendTerms(&terms, &seen, fact.colors);
        appendTerms(&terms, &seen, fact.materials);
        appendTerms(&terms, &seen, fact.attributes);
    }
    return terms;
}

QString VisualAnalysisMetadata::samplingPolicy(VideoFrameExtractionStrategy strategy,
                                               double intervalSeconds,
                                               double sceneThreshold,
                                               double minimumSharpness,
                                               int maxWidth,
                                               int maxHeight)
{
    const auto effectiveInterval = strategy == VideoFrameExtractionStrategy::HighFidelity
        ? qBound(0.1, intervalSeconds, 0.25)
        : qBound(0.1, intervalSeconds, 240.0);
    const auto sceneValue = strategy == VideoFrameExtractionStrategy::SceneAndInterval
        ? QString::number(qBound(0.05, sceneThreshold, 0.95), 'f', 3)
        : QStringLiteral("na");
    return QStringLiteral("filmstoryboard_candidate_sampling_v2|strategy=%1|interval=%2|scene=%3|sharpness=%4|max=%5x%6")
        .arg(static_cast<int>(strategy))
        .arg(effectiveInterval, 0, 'f', 3)
        .arg(sceneValue)
        .arg(qBound(0.0, minimumSharpness, 1.0), 0, 'f', 4)
        .arg(qMax(1, maxWidth))
        .arg(qMax(1, maxHeight));
}

bool VisualAnalysisMetadata::isCurrentSamplingPolicy(const QString &policy)
{
    return policy.startsWith(QStringLiteral("filmstoryboard_candidate_sampling_v2|"));
}

int VisualAnalysisMetadata::fixedFrameInterval(AnalysisMode mode, int configuredInterval)
{
    if (mode == AnalysisMode::EveryFrame) {
        return 1;
    }
    if (mode == AnalysisMode::Every10Frames) {
        return 10;
    }
    return qMax(1, configuredInterval);
}

QVector<int> VisualAnalysisMetadata::plannedFrameNumbers(int sourceFrameCount, int frameInterval)
{
    QVector<int> numbers;
    const auto count = qMax(0, sourceFrameCount);
    const auto interval = qMax(1, frameInterval);
    if (count == 0) {
        return numbers;
    }

    numbers.reserve((count + interval - 1) / interval);
    for (int zeroBased = 0; zeroBased < count; zeroBased += interval) {
        numbers.append(zeroBased + 1);
    }
    if (numbers.constLast() != count) {
        numbers.append(count);
    }
    return numbers;
}

QVector<int> VisualAnalysisMetadata::contactSheetFrameNumbers(int sourceFrameCount,
                                                              int contactSheetFrameCount)
{
    QVector<int> numbers;
    const auto count = qMax(0, sourceFrameCount);
    if (count == 0 || contactSheetFrameCount <= 0) {
        return numbers;
    }

    const auto targetCount = qMin(count, contactSheetFrameCount);
    numbers.reserve(targetCount);
    if (targetCount == 1) {
        numbers.append(1);
        return numbers;
    }

    const auto denominator = static_cast<qint64>(targetCount - 1);
    for (int index = 0; index < targetCount; ++index) {
        const auto numerator = static_cast<qint64>(count - 1) * index;
        const auto zeroBased = (numerator + denominator / 2) / denominator;
        numbers.append(static_cast<int>(zeroBased) + 1);
    }
    return numbers;
}

QVector<int> VisualAnalysisMetadata::plannedFrameNumbers(int sourceFrameCount,
                                                         int frameInterval,
                                                         int contactSheetFrameCount)
{
    auto numbers = plannedFrameNumbers(sourceFrameCount, frameInterval);
    const auto contactSheetNumbers = contactSheetFrameNumbers(sourceFrameCount, contactSheetFrameCount);
    numbers.reserve(numbers.size() + contactSheetNumbers.size());
    for (const auto frameNumber : contactSheetNumbers) {
        numbers.append(frameNumber);
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    return numbers;
}

bool VisualAnalysisMetadata::isFrameAnalysisComplete(const FrameAnalysisRecord &frame,
                                                     int requiredProfileVersion)
{
    return frame.analysisState == FrameAnalysisState::Success
        && frame.errorMessage.trimmed().isEmpty()
        && frame.factsComplete
        && frame.structuredProfileVersion >= requiredProfileVersion;
}

QVector<int> VisualAnalysisMetadata::incompletePlannedFrameNumbers(
    int sourceFrameCount,
    int frameInterval,
    const QVector<FrameAnalysisRecord> &frames,
    int requiredProfileVersion)
{
    return incompletePlannedFrameNumbers(
        sourceFrameCount,
        frameInterval,
        0,
        frames,
        requiredProfileVersion);
}

QVector<int> VisualAnalysisMetadata::incompletePlannedFrameNumbers(
    int sourceFrameCount,
    int frameInterval,
    int contactSheetFrameCount,
    const QVector<FrameAnalysisRecord> &frames,
    int requiredProfileVersion)
{
    QHash<int, FrameAnalysisRecord> byNumber;
    for (const auto &frame : frames) {
        byNumber.insert(frame.frameNumber, frame);
    }

    QVector<int> incomplete;
    for (const auto frameNumber : plannedFrameNumbers(
             sourceFrameCount,
             frameInterval,
             contactSheetFrameCount)) {
        if (!byNumber.contains(frameNumber)
            || !isFrameAnalysisComplete(byNumber.value(frameNumber), requiredProfileVersion)) {
            incomplete.append(frameNumber);
        }
    }
    return incomplete;
}
