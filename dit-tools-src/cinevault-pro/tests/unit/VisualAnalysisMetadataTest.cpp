#include "shared/VisualAnalysisMetadata.h"

#include <QtTest>

class VisualAnalysisMetadataTest : public QObject {
    Q_OBJECT

private slots:
    void entityFacts_roundTripPreservesSameEntityBindings()
    {
        VisionEntityFact shorts;
        shorts.category = QStringLiteral("clothing");
        shorts.label = QStringLiteral("短裤");
        shorts.colors = {QStringLiteral("红色")};
        shorts.materials = {QStringLiteral("牛仔")};
        shorts.attributes = {QStringLiteral("破洞")};

        VisionEntityFact shirt;
        shirt.category = QStringLiteral("clothing");
        shirt.label = QStringLiteral("衬衫");
        shirt.colors = {QStringLiteral("蓝色")};
        shirt.materials = {QStringLiteral("棉")};

        const auto restored = VisualAnalysisMetadata::entityFactsFromJson(
            VisualAnalysisMetadata::entityFactsToJson({shorts, shirt}));

        QCOMPARE(restored.size(), 2);
        QCOMPARE(restored.at(0).label, QStringLiteral("短裤"));
        QCOMPARE(restored.at(0).colors, QStringList{QStringLiteral("红色")});
        QCOMPARE(restored.at(0).materials, QStringList{QStringLiteral("牛仔")});
        QCOMPARE(restored.at(1).label, QStringLiteral("衬衫"));
        QCOMPARE(restored.at(1).colors, QStringList{QStringLiteral("蓝色")});
    }

    void plannedFrameNumbers_usesFixedIntervalWithTerminalCoverage()
    {
        QCOMPARE(VisualAnalysisMetadata::fixedFrameInterval(AnalysisMode::EveryFrame, 99), 1);
        QCOMPARE(VisualAnalysisMetadata::fixedFrameInterval(AnalysisMode::Every10Frames, 3), 10);
        QCOMPARE(VisualAnalysisMetadata::fixedFrameInterval(AnalysisMode::CustomInterval, 7), 7);
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(0, 15), QVector<int>());
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(1, 15), QVector<int>({1}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(5, 1), QVector<int>({1, 2, 3, 4, 5}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(16, 15), QVector<int>({1, 16}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(17, 15), QVector<int>({1, 16, 17}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(10, 30), QVector<int>({1, 10}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(25, 10), QVector<int>({1, 11, 21, 25}));

        const auto sixtyFpsSample = VisualAnalysisMetadata::plannedFrameNumbers(1260, 15);
        QCOMPARE(sixtyFpsSample.size(), 85);
        QCOMPARE(sixtyFpsSample.constLast(), 1260);

        const auto twentyFiveFpsSample = VisualAnalysisMetadata::plannedFrameNumbers(1462, 15);
        QCOMPARE(twentyFiveFpsSample.size(), 99);
        QCOMPARE(twentyFiveFpsSample.constLast(), 1462);
    }

    void samplingPolicy_tracksEveryExtractionInput()
    {
        const auto baseline = VisualAnalysisMetadata::samplingPolicy(
            VideoFrameExtractionStrategy::SceneAndInterval, 1.0, 0.3, 0.01, 1920, 1080);
        QVERIFY(VisualAnalysisMetadata::isCurrentSamplingPolicy(baseline));
        QVERIFY(!VisualAnalysisMetadata::isCurrentSamplingPolicy(
            QStringLiteral("filmstoryboard_candidate_sampling_v1")));
        QVERIFY(baseline != VisualAnalysisMetadata::samplingPolicy(
            VideoFrameExtractionStrategy::IntervalOnly, 1.0, 0.3, 0.01, 1920, 1080));
        QVERIFY(baseline != VisualAnalysisMetadata::samplingPolicy(
            VideoFrameExtractionStrategy::SceneAndInterval, 2.0, 0.3, 0.01, 1920, 1080));
        QVERIFY(baseline != VisualAnalysisMetadata::samplingPolicy(
            VideoFrameExtractionStrategy::SceneAndInterval, 1.0, 0.4, 0.01, 1920, 1080));
        QVERIFY(baseline != VisualAnalysisMetadata::samplingPolicy(
            VideoFrameExtractionStrategy::SceneAndInterval, 1.0, 0.3, 0.02, 1920, 1080));
        QVERIFY(baseline != VisualAnalysisMetadata::samplingPolicy(
            VideoFrameExtractionStrategy::SceneAndInterval, 1.0, 0.3, 0.01, 1280, 720));
        QCOMPARE(VisualAnalysisMetadata::samplingPolicy(
                     VideoFrameExtractionStrategy::IntervalOnly, 1.0, 0.3, 0.01, 1920, 1080),
                 VisualAnalysisMetadata::samplingPolicy(
                     VideoFrameExtractionStrategy::IntervalOnly, 1.0, 0.9, 0.01, 1920, 1080));
    }

    void plannedFrameNumbers_mergesContactSheetCoverageWithRuleFrames()
    {
        QCOMPARE(VisualAnalysisMetadata::contactSheetFrameNumbers(0, 24), QVector<int>());
        QCOMPARE(VisualAnalysisMetadata::contactSheetFrameNumbers(1, 24), QVector<int>({1}));
        QCOMPARE(VisualAnalysisMetadata::contactSheetFrameNumbers(5, 1), QVector<int>({1}));
        QCOMPARE(VisualAnalysisMetadata::contactSheetFrameNumbers(5, 3), QVector<int>({1, 3, 5}));
        QCOMPARE(VisualAnalysisMetadata::contactSheetFrameNumbers(5, 24),
                 QVector<int>({1, 2, 3, 4, 5}));

        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(17, 15, 4),
                 QVector<int>({1, 6, 12, 16, 17}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(16, 15, 4),
                 QVector<int>({1, 6, 11, 16}));
        QCOMPARE(VisualAnalysisMetadata::plannedFrameNumbers(5, 1, 4),
                 QVector<int>({1, 2, 3, 4, 5}));

        const auto realSample = VisualAnalysisMetadata::plannedFrameNumbers(1260, 15, 24);
        QCOMPARE(realSample.size(), 106);
        QCOMPARE(realSample.constFirst(), 1);
        QCOMPARE(realSample.constLast(), 1260);
        for (const auto frameNumber : VisualAnalysisMetadata::contactSheetFrameNumbers(1260, 24)) {
            QVERIFY(realSample.contains(frameNumber));
        }
    }

    void incompletePlan_detectsMissingFailedAndLegacyFramesOnly()
    {
        FrameAnalysisRecord complete;
        complete.frameNumber = 1;
        complete.analysisState = FrameAnalysisState::Success;
        complete.factsComplete = true;
        complete.structuredProfileVersion = 2;

        FrameAnalysisRecord legacy = complete;
        legacy.frameNumber = 11;
        legacy.factsComplete = false;
        legacy.structuredProfileVersion = 1;

        FrameAnalysisRecord failed = complete;
        failed.frameNumber = 21;
        failed.analysisState = FrameAnalysisState::Failed;

        QCOMPARE(VisualAnalysisMetadata::incompletePlannedFrameNumbers(
                     35, 10, {complete, legacy, failed}, 2),
                 QVector<int>({11, 21, 31, 35}));
    }

    void legacyFixedIntervalPlan_detectsOnlyMissingTerminalFrame()
    {
        FrameAnalysisRecord first;
        first.frameNumber = 1;
        first.analysisState = FrameAnalysisState::Success;
        first.factsComplete = true;
        first.structuredProfileVersion = 2;

        FrameAnalysisRecord intervalPoint = first;
        intervalPoint.frameNumber = 16;

        QCOMPARE(VisualAnalysisMetadata::incompletePlannedFrameNumbers(
                     17, 15, {first, intervalPoint}, 2),
                 QVector<int>({17}));
    }

    void legacyFixedIntervalPlan_detectsMissingContactSheetFrames()
    {
        FrameAnalysisRecord first;
        first.frameNumber = 1;
        first.analysisState = FrameAnalysisState::Success;
        first.factsComplete = true;
        first.structuredProfileVersion = 2;

        FrameAnalysisRecord intervalPoint = first;
        intervalPoint.frameNumber = 16;

        FrameAnalysisRecord last = first;
        last.frameNumber = 17;

        QCOMPARE(VisualAnalysisMetadata::incompletePlannedFrameNumbers(
                     17, 15, 4, {first, intervalPoint, last}, 2),
                 QVector<int>({6, 12}));
    }
};

QTEST_GUILESS_MAIN(VisualAnalysisMetadataTest)

#include "VisualAnalysisMetadataTest.moc"
