#include "core/search/NaturalLanguageQueryParser.h"

#include <QtTest>

class NaturalLanguageQueryParserTest : public QObject {
    Q_OBJECT

private slots:
    void parsesDateTypeAndStrictSameEntityConstraint()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(
            QStringLiteral("帮我找 2026年7月14日 上海拍摄的红色牛仔短裤视频"),
            QDate(2026, 7, 20));

        QCOMPARE(query.normalizedDate, QStringLiteral("2026-07-14"));
        QCOMPARE(query.assetTypeFilter, static_cast<int>(AssetType::Video));
        QCOMPARE(query.strictEntities.size(), 1);
        QCOMPARE(query.strictEntities.first().label, QStringLiteral("短裤"));
        QCOMPARE(query.strictEntities.first().colors, QStringList{QStringLiteral("红色")});
        QVERIFY(query.strictEntities.first().materials.contains(QStringLiteral("牛仔")));
        QVERIFY(query.lexicalTerms.contains(QStringLiteral("上海")));
        QCOMPARE(query.dateConstraint.preferredField, SearchDateField::CapturedTime);
        QVERIFY(!query.semanticText.contains(QStringLiteral("拍摄")));
    }

    void parsesRelativeFolderDate()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("昨天的拍摄目录"), QDate(2026, 7, 14));

        QCOMPARE(query.normalizedDate, QStringLiteral("2026-07-13"));
        QVERIFY(query.folderIntent);
        QCOMPARE(query.assetTypeFilter, -1);
        QCOMPARE(query.dateConstraint.preferredField, SearchDateField::FolderDate);
        QVERIFY(query.lexicalTerms.isEmpty());
        QVERIFY(query.semanticText.isEmpty());
    }

    void invalidDateIsNotAcceptedAsFilter()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("2026年2月30日的图片"), QDate(2026, 7, 14));

        QVERIFY(query.normalizedDate.isEmpty());
        QCOMPARE(query.assetTypeFilter, static_cast<int>(AssetType::Image));
    }

    void plainTextRemainsUsableForLexicalAndSemanticSearch()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("上海夜景航拍"));

        QCOMPARE(query.semanticText, QStringLiteral("上海夜景航拍"));
        QVERIFY(query.lexicalTerms.contains(QStringLiteral("上海夜景航拍")));
        QVERIFY(query.lexicalTerms.contains(QStringLiteral("夜景")));
        QVERIFY(query.lexicalTerms.contains(QStringLiteral("航拍")));
        QVERIFY(query.strictEntities.isEmpty());
    }

    void entityNameDoesNotCreateRedundantMaterialConstraint()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("蓝色的牛仔裤"));

        QCOMPARE(query.strictEntities.size(), 1);
        QCOMPARE(query.strictEntities.first().label, QStringLiteral("牛仔裤"));
        QCOMPARE(query.strictEntities.first().colors, QStringList{QStringLiteral("蓝色")});
        QVERIFY(query.strictEntities.first().materials.isEmpty());
        QVERIFY(!query.interpretationLabels.contains(QStringLiteral("同一对象：牛仔裤 蓝色 牛仔")));
    }

    void captureDateIntentDoesNotBecomeRequiredKeyword()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("昨天拍摄的素材"), QDate(2026, 7, 14));

        QCOMPARE(query.dateConstraint.startDate, QStringLiteral("2026-07-13"));
        QCOMPARE(query.dateConstraint.endDate, QStringLiteral("2026-07-13"));
        QCOMPARE(query.dateConstraint.preferredField, SearchDateField::CapturedTime);
        QVERIFY(query.lexicalTerms.isEmpty());
        QVERIFY(query.semanticText.isEmpty());
        QVERIFY(query.interpretationLabels.contains(QStringLiteral("拍摄日期：2026-07-13")));
    }

    void parsesRelativeDateRange()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("最近7天的夜景视频"), QDate(2026, 7, 14));

        QCOMPARE(query.dateConstraint.startDate, QStringLiteral("2026-07-08"));
        QCOMPARE(query.dateConstraint.endDate, QStringLiteral("2026-07-14"));
        QVERIFY(query.normalizedDate.isEmpty());
        QCOMPARE(query.assetTypeFilter, static_cast<int>(AssetType::Video));
        QCOMPARE(query.semanticText, QStringLiteral("夜景"));
    }

    void parsesCalendarYearWordsAsStrictRanges()
    {
        NaturalLanguageQueryParser parser;

        const auto lastYear = parser.parse(QStringLiteral("去年的视频"), QDate(2026, 7, 31));
        QCOMPARE(lastYear.dateConstraint.startDate, QStringLiteral("2025-01-01"));
        QCOMPARE(lastYear.dateConstraint.endDate, QStringLiteral("2025-12-31"));
        QCOMPARE(lastYear.dateConstraint.matchedText, QStringLiteral("去年"));
        QCOMPARE(lastYear.assetTypeFilter, static_cast<int>(AssetType::Video));
        QVERIFY(lastYear.semanticText.isEmpty());

        const auto thisYear = parser.parse(QStringLiteral("今年的素材"), QDate(2026, 7, 31));
        QCOMPARE(thisYear.dateConstraint.startDate, QStringLiteral("2026-01-01"));
        QCOMPARE(thisYear.dateConstraint.endDate, QStringLiteral("2026-07-31"));

        const auto explicitYear = parser.parse(QStringLiteral("2025年的视频"), QDate(2026, 7, 31));
        QCOMPARE(explicitYear.dateConstraint.startDate, QStringLiteral("2025-01-01"));
        QCOMPARE(explicitYear.dateConstraint.endDate, QStringLiteral("2025-12-31"));
        QCOMPARE(explicitYear.dateConstraint.matchedText, QStringLiteral("2025年"));
    }

    void parsesOneCalendarMonthAgoAsExactDate()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("搜索一个月前的视频素材"),
                                        QDate(2026, 7, 31));

        QCOMPARE(query.dateConstraint.startDate, QStringLiteral("2026-06-30"));
        QCOMPARE(query.dateConstraint.endDate, QStringLiteral("2026-06-30"));
        QCOMPARE(query.dateConstraint.matchedText, QStringLiteral("一个月前"));
        QCOMPARE(query.assetTypeFilter, static_cast<int>(AssetType::Video));
        QVERIFY(query.semanticText.isEmpty());
    }

    void parsesBeforeOneMonthAsCutoffRange()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("一个月之前的图片"),
                                        QDate(2026, 7, 15));

        QCOMPARE(query.dateConstraint.startDate, QStringLiteral("0001-01-01"));
        QCOMPARE(query.dateConstraint.endDate, QStringLiteral("2026-06-14"));
        QCOMPARE(query.dateConstraint.matchedText, QStringLiteral("一个月之前"));
        QCOMPARE(query.assetTypeFilter, static_cast<int>(AssetType::Image));
    }

    void parsesRecentCalendarMonthAsRange()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("最近一个月的夜景视频"),
                                        QDate(2026, 7, 15));

        QCOMPARE(query.dateConstraint.startDate, QStringLiteral("2026-06-15"));
        QCOMPARE(query.dateConstraint.endDate, QStringLiteral("2026-07-15"));
        QCOMPARE(query.semanticText, QStringLiteral("夜景"));
    }

    void keepsCaptureAsVisualMeaningWithoutDate()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("棚内拍摄现场"));

        QCOMPARE(query.semanticText, QStringLiteral("棚内拍摄现场"));
        QVERIFY(query.lexicalTerms.contains(QStringLiteral("棚内拍摄现场")));
        QVERIFY(query.lexicalTerms.contains(QStringLiteral("棚内")));
    }

    void parsesMultipleAssetTypesAsUnion()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("昨天拍摄的视频、图片"), QDate(2026, 7, 14));

        QCOMPARE(query.resultTarget, SearchResultTarget::Assets);
        QCOMPARE(query.assetTypeFilters,
                 QVector<int>({static_cast<int>(AssetType::Video),
                               static_cast<int>(AssetType::Image)}));
        QCOMPARE(query.dateConstraint.startDate, QStringLiteral("2026-07-13"));
        QVERIFY(query.lexicalTerms.isEmpty());
        QVERIFY(query.interpretationLabels.contains(QStringLiteral("类型：视频 / 图片")));
    }

    void explicitFolderIntentReturnsOnlyFolders()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("昨天的文件夹"), QDate(2026, 7, 14));

        QCOMPARE(query.resultTarget, SearchResultTarget::Folders);
        QVERIFY(query.assetTypeFilters.isEmpty());
        QVERIFY(!query.folderByAssetCriteria);
        QVERIFY(query.interpretationLabels.contains(QStringLiteral("目标：文件夹")));
    }

    void parsesFolderContainingAssetCriteria()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(
            QStringLiteral("昨天拍摄的视频和图片所在的文件夹"),
            QDate(2026, 7, 14));

        QCOMPARE(query.resultTarget, SearchResultTarget::Folders);
        QVERIFY(query.folderByAssetCriteria);
        QCOMPARE(query.assetTypeFilters.size(), 2);
        QVERIFY(query.interpretationLabels.contains(QStringLiteral("目标：匹配素材所在的文件夹")));
    }

    void extractsVisualFallbackTermsAndQuotedOcr()
    {
        NaturalLanguageQueryParser parser;
        const auto visual = parser.parse(QStringLiteral("找雨夜里有人撑伞的画面"));
        QCOMPARE(visual.resultTarget, SearchResultTarget::Frames);
        QVERIFY(visual.frameIntent);
        QCOMPARE(visual.semanticText, QStringLiteral("雨夜里有人撑伞"));
        QVERIFY(visual.lexicalTerms.contains(QStringLiteral("雨夜")));
        QVERIFY(visual.lexicalTerms.contains(QStringLiteral("撑伞")));
        QVERIFY(visual.lexicalTerms.contains(QStringLiteral("伞")));

        const auto ocr = parser.parse(QStringLiteral("找画面文字写着“新品发布”的视频"));
        QCOMPARE(ocr.resultTarget, SearchResultTarget::Assets);
        QCOMPARE(ocr.ocrText, QStringLiteral("新品发布"));
        QVERIFY(ocr.lexicalTerms.contains(QStringLiteral("新品发布")));
        QVERIFY(ocr.interpretationLabels.contains(QStringLiteral("画面文字：新品发布")));
    }

    void explicitFrameIntentProducesFrameResultDomain()
    {
        NaturalLanguageQueryParser parser;
        const auto query = parser.parse(QStringLiteral("包含了蓝色牛仔裤的帧"));

        QCOMPARE(query.resultTarget, SearchResultTarget::Frames);
        QVERIFY(query.frameIntent);
        QVERIFY(!query.folderIntent);
        QCOMPARE(query.semanticText, QStringLiteral("蓝色牛仔裤"));
        QCOMPARE(query.strictEntities.size(), 1);
        QCOMPARE(query.strictEntities.first().label, QStringLiteral("牛仔裤"));
        QCOMPARE(query.strictEntities.first().colors, QStringList{QStringLiteral("蓝色")});
        QVERIFY(query.strictEntities.first().materials.isEmpty());
        QVERIFY(query.interpretationLabels.contains(QStringLiteral("目标：视觉帧")));
        QVERIFY(!query.lexicalTerms.contains(QStringLiteral("帧")));
    }

    void pictureAndFrameAreEquivalentResultIntents()
    {
        NaturalLanguageQueryParser parser;
        const auto frame = parser.parse(QStringLiteral("有男人穿着牛仔裤的帧"));
        const auto picture = parser.parse(QStringLiteral("有男人穿着牛仔裤的画面"));

        QCOMPARE(frame.resultTarget, SearchResultTarget::Frames);
        QCOMPARE(picture.resultTarget, SearchResultTarget::Frames);
        QCOMPARE(frame.semanticText, picture.semanticText);
        QVERIFY(frame.strictEntities.isEmpty());
        QVERIFY(picture.strictEntities.isEmpty());
        QCOMPARE(frame.explicitEntityLabels.size(), 2);
        QVERIFY(frame.explicitEntityLabels.contains(QStringLiteral("男人")));
        QVERIFY(frame.explicitEntityLabels.contains(QStringLiteral("牛仔裤")));
        QVERIFY(frame.lexicalTerms.contains(QStringLiteral("男人")));
        QVERIFY(frame.lexicalTerms.contains(QStringLiteral("牛仔裤")));
    }
};

QTEST_GUILESS_MAIN(NaturalLanguageQueryParserTest)

#include "NaturalLanguageQueryParserTest.moc"
