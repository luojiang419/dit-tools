#include "core/search/NaturalLanguageQueryParser.h"
#include "core/search/SearchAssistantRouter.h"
#include "core/search/SearchReliabilityEvaluator.h"

#include <QtTest>

namespace {
MaterialSearchResult baselineFor(const QString &text)
{
    NaturalLanguageQueryParser parser;
    MaterialSearchResult result;
    result.parsedQuery = parser.parse(text, QDate(2026, 7, 15));
    result.reliability = SearchReliabilityEvaluator::evaluate(result);
    return result;
}

GlobalVideoAsset asset(double score,
                       double confidence,
                       const QStringList &reasons)
{
    GlobalVideoAsset value;
    value.videoKey = QStringLiteral("asset-1");
    value.searchScore = score;
    value.searchConfidence = confidence;
    value.searchReasons = reasons;
    return value;
}
}

class SearchAssistantRouterTest : public QObject {
    Q_OBJECT

private slots:
    void skipsDeterministicAndExactQueries()
    {
        auto deterministic = baselineFor(QStringLiteral("搜索一个月前的视频素材"));
        QCOMPARE(SearchAssistantRouter::decide(deterministic, false).route,
                 SearchAssistantRoute::Skip);

        auto exact = baselineFor(QStringLiteral("搜索 A001_C003.mp4"));
        QCOMPARE(SearchAssistantRouter::decide(exact, true).route,
                 SearchAssistantRoute::Skip);
    }

    void requiresAssistantForZeroResultsAndComplexRelations()
    {
        auto empty = baselineFor(QStringLiteral("未来感发布会现场"));
        QCOMPARE(SearchAssistantRouter::decide(empty, false).route,
                 SearchAssistantRoute::Required);

        auto relation = baselineFor(QStringLiteral("男人拿着雨伞站在汽车旁边的画面"));
        QCOMPARE(SearchAssistantRouter::decide(relation, false).route,
                 SearchAssistantRoute::Required);
        QVERIFY(SearchAssistantRouter::decide(relation, false).shouldStartRuntime());
    }

    void skipsStrongDirectEvidence()
    {
        auto result = baselineFor(QStringLiteral("搜索红色牛仔裤"));
        result.assets.append(asset(0.91,
                                   0.94,
                                   {QStringLiteral("查询关键词完整覆盖"),
                                    QStringLiteral("同一视觉对象属性已验证")}));
        result.reliability = SearchReliabilityEvaluator::evaluate(result);

        const auto decision = SearchAssistantRouter::decide(result, true);
        QCOMPARE(decision.route, SearchAssistantRoute::Skip);
    }

    void grayZoneUsesOnlyWarmAssistant()
    {
        auto result = baselineFor(QStringLiteral("城市夜景"));
        result.assets.append(asset(0.70,
                                   0.72,
                                   {QStringLiteral("关键词或视觉文本命中")}));
        result.reliability.score = 0.75;
        result.reliability.shouldUseAssistant = false;
        result.reliability.reasons = {QStringLiteral("关键词或视觉文本命中")};

        const auto cold = SearchAssistantRouter::decide(result, false);
        QCOMPARE(cold.route, SearchAssistantRoute::ReadyOnly);
        QVERIFY(!cold.shouldInvoke(false));
        QVERIFY(!cold.shouldStartRuntime());

        const auto warm = SearchAssistantRouter::decide(result, true);
        QCOMPARE(warm.route, SearchAssistantRoute::ReadyOnly);
        QVERIFY(warm.shouldInvoke(true));
    }
};

QTEST_GUILESS_MAIN(SearchAssistantRouterTest)

#include "SearchAssistantRouterTest.moc"
