#include "core/search/CaptureTimeResolver.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <utility>

namespace {
struct TagCandidate {
    QString value;
    QString source;
    double confidence = 0.0;
    int priority = 0;
    int order = 0;
};

struct NormalizedDateTime {
    QString dateTime;
    QString date;
};

NormalizedDateTime normalizeDateTime(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    for (const auto format : {Qt::ISODateWithMs, Qt::ISODate}) {
        const auto dateTime = QDateTime::fromString(value, format);
        if (dateTime.isValid()) {
            return {dateTime.toString(Qt::ISODateWithMs),
                    dateTime.date().toString(Qt::ISODate)};
        }
    }

    const QStringList dateTimeFormats{
        QStringLiteral("yyyy:MM:dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"),
        QStringLiteral("yyyy.MM.dd HH:mm:ss"),
        QStringLiteral("yyyy:MM:dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")
    };
    for (const auto &format : dateTimeFormats) {
        const auto dateTime = QDateTime::fromString(value, format);
        if (dateTime.isValid()) {
            return {dateTime.toString(Qt::ISODateWithMs),
                    dateTime.date().toString(Qt::ISODate)};
        }
    }

    const QStringList dateFormats{
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("yyyy/MM/dd"),
        QStringLiteral("yyyy.MM.dd"),
        QStringLiteral("yyyy:MM:dd")
    };
    for (const auto &format : dateFormats) {
        const auto date = QDate::fromString(value, format);
        if (date.isValid()) {
            return {QString(), date.toString(Qt::ISODate)};
        }
    }

    static const QRegularExpression leadingDate(
        QStringLiteral("^((?:19|20)\\d{2})[-:/.](\\d{1,2})[-:/.](\\d{1,2})"));
    const auto match = leadingDate.match(value);
    if (match.hasMatch()) {
        const QDate date(match.captured(1).toInt(),
                         match.captured(2).toInt(),
                         match.captured(3).toInt());
        if (date.isValid()) {
            return {QString(), date.toString(Qt::ISODate)};
        }
    }
    return {};
}

void appendTagCandidates(const QJsonObject &tags,
                         int *order,
                         QVector<TagCandidate> *candidates)
{
    for (auto iterator = tags.constBegin(); iterator != tags.constEnd(); ++iterator) {
        const auto key = iterator.key().trimmed().toCaseFolded();
        TagCandidate candidate;
        candidate.value = iterator.value().toVariant().toString().trimmed();
        candidate.order = (*order)++;
        if (candidate.value.isEmpty()) {
            continue;
        }
        if (key == QStringLiteral("com.apple.quicktime.creationdate")
            || key == QStringLiteral("com.apple.quicktime.creation_date")) {
            candidate.source = QStringLiteral("quicktime_creation_date");
            candidate.confidence = 1.0;
            candidate.priority = 400;
        } else if (key == QStringLiteral("datetimeoriginal")
                   || key == QStringLiteral("date_time_original")
                   || key == QStringLiteral("exif.datetimeoriginal")) {
            candidate.source = QStringLiteral("exif_datetime_original");
            candidate.confidence = 0.98;
            candidate.priority = 350;
        } else if (key == QStringLiteral("creation_time")) {
            candidate.source = QStringLiteral("media_creation_time");
            candidate.confidence = 0.90;
            candidate.priority = 300;
        } else {
            continue;
        }
        candidates->append(std::move(candidate));
    }
}

QVector<TagCandidate> candidatesFromJson(const QString &ffprobeJson)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(ffprobeJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    QVector<TagCandidate> candidates;
    int order = 0;
    const auto root = document.object();
    appendTagCandidates(root.value(QStringLiteral("format")).toObject()
                            .value(QStringLiteral("tags")).toObject(),
                        &order,
                        &candidates);
    const auto streams = root.value(QStringLiteral("streams")).toArray();
    for (const auto &stream : streams) {
        appendTagCandidates(stream.toObject().value(QStringLiteral("tags")).toObject(),
                            &order,
                            &candidates);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        if (left.priority != right.priority) {
            return left.priority > right.priority;
        }
        return left.order < right.order;
    });
    return candidates;
}
}

CaptureTimeInfo CaptureTimeResolver::resolve(const QString &ffprobeJson,
                                             const QString &folderDate,
                                             const QString &modifiedAt) const
{
    for (const auto &candidate : candidatesFromJson(ffprobeJson)) {
        const auto normalized = normalizeDateTime(candidate.value);
        if (normalized.date.isEmpty()) {
            continue;
        }
        return {normalized.dateTime,
                normalized.date,
                candidate.source,
                candidate.confidence,
                false};
    }

    const auto normalizedFolderDate = normalizeDateTime(folderDate);
    if (!normalizedFolderDate.date.isEmpty()) {
        return {QString(),
                normalizedFolderDate.date,
                QStringLiteral("folder_date"),
                0.55,
                true};
    }

    const auto normalizedModifiedAt = normalizeDateTime(modifiedAt);
    if (!normalizedModifiedAt.date.isEmpty()) {
        return {normalizedModifiedAt.dateTime,
                normalizedModifiedAt.date,
                QStringLiteral("file_modified_time"),
                0.25,
                true};
    }
    return {};
}
