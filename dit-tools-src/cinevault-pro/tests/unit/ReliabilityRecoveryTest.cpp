#include "application/AnalysisRunStore.h"
#include "shared/FrameRequestSchedule.h"
#include "shared/LatestRequestQueue.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ReliabilityRecoveryTest : public QObject {
    Q_OBJECT
private slots:
    void requestsRetainOnlyLatestWhileWorkerIsBlocked()
    {
        LatestRequestQueue queue;
        QVector<int> started;
        queue.submit([&]() { started.append(0); });
        for (int i = 1; i <= 100000; ++i) {
            queue.submit([&, i]() { started.append(i); });
        }
        QCOMPARE(started, QVector<int>{0});
        queue.complete();
        QCOMPARE(started, QVector<int>({0, 100000}));
        queue.complete();
        queue.submit([&]() { started.append(100001); queue.complete(); });
        QCOMPARE(started, QVector<int>({0, 100000, 100001}));
    }

    void exhaustedOrIncompleteFramesAreDispatchedOncePerRun()
    {
        QVector<FrameAnalysisRecord> frames(25);
        for (auto &frame : frames) {
            frame.analysisState = FrameAnalysisState::Skipped;
            frame.retryCount = 3;
        }
        frames[0].analysisState = FrameAnalysisState::Success;
        frames[0].factsComplete = false;
        FrameRequestSchedule schedule;
        QSet<int> visited;
        for (int round = 0; round < 4; ++round) {
            const auto batch = schedule.takeNext(frames, 200, 2);
            QVERIFY(batch.size() <= FrameRequestSchedule::MaximumConcurrency);
            for (const auto index : batch) {
                QVERIFY(!visited.contains(index));
                visited.insert(index);
            }
        }
        QCOMPARE(visited.size(), frames.size());
        QVERIFY(schedule.takeNext(frames, 200, 2).isEmpty());
        FrameRequestSchedule resumed;
        QCOMPARE(resumed.takeNext(frames, 200, 2).size(), 8);
    }

    void rebuildCheckpointSurvivesRestartAndPreservesActiveImages()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto run = QDir(temp.path()).filePath(QStringLiteral("runs/pending-test"));
        QVERIFY(QDir().mkpath(run));
        QFile oldImage(QDir(temp.path()).filePath(QStringLiteral("old.jpg")));
        QVERIFY(oldImage.open(QIODevice::WriteOnly));
        oldImage.write("old generation");
        oldImage.close();
        FrameAnalysisRecord frame;
        frame.videoKey = QStringLiteral("video-key");
        frame.frameNumber = 1;
        frame.timestampMs = 9876543210LL;
        frame.imagePath = QDir(run).filePath(QStringLiteral("frame.jpg"));
        QFile image(frame.imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("new generation");
        image.close();
        VisualAnalysisPlan plan;
        plan.videoKey = frame.videoKey;
        plan.samplingPolicy = QStringLiteral("test-policy");
        plan.assetSizeBytes = 98765432101LL;
        plan.assetModifiedAt = QStringLiteral("2026-09-07");
        plan.plannedFrameCount = 1;
        QString error;
        {
            AnalysisRunStore store(temp.path());
            QVERIFY2(store.begin(plan, {frame}, QStringLiteral("model"), &error), qPrintable(error));
            frame.caption = QStringLiteral("已付费完成的场景描述");
            frame.analysisState = FrameAnalysisState::Success;
            frame.factsComplete = true;
            frame.structuredProfileVersion = 2;
            frame.tags = {QStringLiteral("山川")};
            VisionEntityFact entity;
            entity.label = QStringLiteral("汽车");
            entity.colors = {QStringLiteral("红色")};
            frame.entities = {entity};
            QVERIFY2(store.saveFrame(frame, &error), qPrintable(error));
        }
        AnalysisRunStore restored(temp.path());
        VisualAnalysisPlan loaded;
        QVector<FrameAnalysisRecord> rows;
        bool found = false;
        QVERIFY2(restored.load(plan, QStringLiteral("model"), &loaded, &rows, &found, &error), qPrintable(error));
        QVERIFY(found);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().caption, frame.caption);
        QCOMPARE(rows.first().timestampMs, frame.timestampMs);
        QCOMPARE(rows.first().tags, frame.tags);
        QCOMPARE(rows.first().entities.first().colors, frame.entities.first().colors);
        QCOMPARE(rows.first().analysisState, FrameAnalysisState::Success);
        QVERIFY(VisualAnalysisMetadata::isFrameAnalysisComplete(rows.first(), 2));
        QCOMPARE(loaded.assetSizeBytes, plan.assetSizeBytes);
        QVERIFY(oldImage.open(QIODevice::ReadOnly));
        QCOMPARE(oldImage.readAll(), QByteArray("old generation"));
        oldImage.close();

        auto changed = plan;
        changed.assetModifiedAt = QStringLiteral("2026-09-08");
        QVERIFY(restored.load(changed, QStringLiteral("model"), &loaded, &rows, &found, &error));
        QVERIFY(!found);
        QVERIFY(restored.load(plan, QStringLiteral("different-model"), &loaded, &rows, &found, &error));
        QVERIFY(!found);

        QFile corrupted(QDir(run).filePath(QStringLiteral("analysis-1.json")));
        QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupted.write("broken");
        corrupted.close();
        QVERIFY(!restored.load(plan, QStringLiteral("model"), &loaded, &rows, &found, &error));
        QVERIFY(!error.isEmpty());
        restored.markPublished();
        QVERIFY(!QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("pending-run.json"))));
        QVERIFY(QFileInfo::exists(frame.imagePath));
        QVERIFY(QFileInfo::exists(oldImage.fileName()));
    }
};

QTEST_GUILESS_MAIN(ReliabilityRecoveryTest)
#include "ReliabilityRecoveryTest.moc"
