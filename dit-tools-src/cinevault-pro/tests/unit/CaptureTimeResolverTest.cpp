#include "core/search/CaptureTimeResolver.h"

#include <QtTest>

class CaptureTimeResolverTest : public QObject {
    Q_OBJECT

private slots:
    void quickTimeCreationDateHasHighestPriority()
    {
        const QString json = QStringLiteral(R"({
            "format": {"tags": {
                "creation_time": "2026-07-13T01:00:00Z",
                "com.apple.quicktime.creationdate": "2026-07-14T09:30:00+08:00"
            }}
        })");

        const auto result = CaptureTimeResolver().resolve(
            json,
            QStringLiteral("2026-07-12"),
            QStringLiteral("2026-07-15T10:00:00"));

        QCOMPARE(result.captureDate, QStringLiteral("2026-07-14"));
        QCOMPARE(result.source, QStringLiteral("quicktime_creation_date"));
        QCOMPARE(result.confidence, 1.0);
        QVERIFY(!result.fallback);
    }

    void exifDateTimeOriginalIsUnderstood()
    {
        const QString json = QStringLiteral(R"({
            "streams": [{"tags": {"DateTimeOriginal": "2026:07:13 23:59:58"}}]
        })");

        const auto result = CaptureTimeResolver().resolve(json, {}, {});

        QCOMPARE(result.captureDate, QStringLiteral("2026-07-13"));
        QCOMPARE(result.source, QStringLiteral("exif_datetime_original"));
        QCOMPARE(result.confidence, 0.98);
    }

    void invalidMediaDateFallsBackToFolderDate()
    {
        const QString json = QStringLiteral(R"({
            "format": {"tags": {"creation_time": "not-a-date"}}
        })");

        const auto result = CaptureTimeResolver().resolve(
            json,
            QStringLiteral("2026-07-12"),
            QStringLiteral("2026-07-15T10:00:00"));

        QCOMPARE(result.captureDate, QStringLiteral("2026-07-12"));
        QCOMPARE(result.source, QStringLiteral("folder_date"));
        QCOMPARE(result.confidence, 0.55);
        QVERIFY(result.fallback);
        QVERIFY(result.captureTime.isEmpty());
    }

    void modifiedTimeIsLastResortAndIsMarkedAsFallback()
    {
        const auto result = CaptureTimeResolver().resolve(
            {},
            {},
            QStringLiteral("2026-07-11T18:20:30"));

        QCOMPARE(result.captureDate, QStringLiteral("2026-07-11"));
        QCOMPARE(result.source, QStringLiteral("file_modified_time"));
        QCOMPARE(result.confidence, 0.25);
        QVERIFY(result.fallback);
    }
};

QTEST_GUILESS_MAIN(CaptureTimeResolverTest)

#include "CaptureTimeResolverTest.moc"
