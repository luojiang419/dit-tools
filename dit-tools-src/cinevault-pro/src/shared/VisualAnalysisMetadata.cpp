#include "shared/VisualAnalysisMetadata.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

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
    QHash<int, FrameAnalysisRecord> byNumber;
    for (const auto &frame : frames) {
        byNumber.insert(frame.frameNumber, frame);
    }

    QVector<int> incomplete;
    for (const auto frameNumber : plannedFrameNumbers(sourceFrameCount, frameInterval)) {
        if (!byNumber.contains(frameNumber)
            || !isFrameAnalysisComplete(byNumber.value(frameNumber), requiredProfileVersion)) {
            incomplete.append(frameNumber);
        }
    }
    return incomplete;
}
