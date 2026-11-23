#include "infrastructure/monitoring/PerformanceTelemetry.h"

#include <QtTest>

class PerformanceTelemetryTest final : public QObject {
    Q_OBJECT

private slots:
    void snapshotTracksBoundedQueueAndAggregates()
    {
        auto &telemetry = PerformanceTelemetry::global();
        telemetry.resetForTesting();

        telemetry.setQueueDepth(QStringLiteral("scan.directory_entries"), 10);
        telemetry.setQueueDepth(QStringLiteral("scan.directory_entries"), 4);
        telemetry.recordBatch(QStringLiteral("scan"), QStringLiteral("enumerate"), 10, 20);
        telemetry.recordBatch(QStringLiteral("scan"), QStringLiteral("enumerate"), 4, 5);
        telemetry.recordSqliteBusy(QStringLiteral("scan"));

        const auto snapshot = telemetry.snapshot();
        QCOMPARE(snapshot.value(QStringLiteral("sqlite_busy_count")).toInteger(), qint64{1});
        QCOMPARE(snapshot.value(QStringLiteral("queue_depths")).toObject()
                     .value(QStringLiteral("scan.directory_entries")).toInteger(),
                 qint64{4});
        QCOMPARE(snapshot.value(QStringLiteral("peak_queue_depths")).toObject()
                     .value(QStringLiteral("scan.directory_entries")).toInteger(),
                 qint64{10});
        QCOMPARE(snapshot.value(QStringLiteral("stage_batch_counts")).toObject()
                     .value(QStringLiteral("scan.enumerate")).toInteger(),
                 qint64{2});
        QCOMPARE(snapshot.value(QStringLiteral("stage_item_counts")).toObject()
                     .value(QStringLiteral("scan.enumerate")).toInteger(),
                 qint64{14});
        QCOMPARE(snapshot.value(QStringLiteral("stage_elapsed_ms")).toObject()
                     .value(QStringLiteral("scan.enumerate")).toInteger(),
                 qint64{25});
    }

    void heartbeatReportsP95WithoutUnboundedHistory()
    {
        auto &telemetry = PerformanceTelemetry::global();
        telemetry.resetForTesting();
        for (int index = 0; index < 2200; ++index) {
            telemetry.recordUiHeartbeat(index % 101);
        }

        const auto snapshot = telemetry.snapshot();
        QCOMPARE(snapshot.value(QStringLiteral("ui_heartbeat_samples")).toInteger(), qint64{2048});
        QVERIFY(snapshot.value(QStringLiteral("ui_heartbeat_p95_ms")).toInteger() >= 94);
        QCOMPARE(snapshot.value(QStringLiteral("ui_heartbeat_max_ms")).toInteger(), qint64{100});
    }

    void processMemorySampleIsAvailableOnWindows()
    {
        const auto sample = PerformanceTelemetry::sampleProcessMemory();
#ifdef Q_OS_WIN
        QVERIFY(sample.workingSetBytes > 0);
        QVERIFY(sample.privateBytes > 0);
#else
        QCOMPARE(sample.workingSetBytes, qint64{-1});
        QCOMPARE(sample.privateBytes, qint64{-1});
#endif
    }
};

QTEST_GUILESS_MAIN(PerformanceTelemetryTest)

#include "PerformanceTelemetryTest.moc"
