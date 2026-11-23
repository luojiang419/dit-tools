#include "core/search/NaturalLanguageQueryParser.h"
#include "core/search/SearchReliabilityEvaluator.h"

#include <QtTest>

namespace {
MaterialSearchResult resultFor(const QString &text)
{
    NaturalLanguageQueryParser parser;
    MaterialSearchResult result;
    result.parsedQuery = parser.parse(text, QDate(2026, 7, 15));
    return result;
}

GlobalVideoAsset asset(double score,
                       double confidence,
                       const QStringList &reasons = {})
{
    GlobalVideoAsset item;
    item.videoKey = QStringLiteral("asset-1");
    item.searchScore = score;
    item.searchConfidence = confidence;
    item.searchReasons = reasons;
    return item;
}
}

class SearchReliabilityEvaluatorTest : public QObject {
    Q_OBJECT

private slots:
    void deterministicDateAndTypeNeverNeedModel()
    {
        const auto result = resultFor(QStringLiteral("搜索一个月前的视频素材"));
        const auto assessment = SearchReliabilityEvaluator::evaluate(result);

        QVERIFY(!assessment.shouldUseAssistant);
        QCOMPARE(assessment.score, 1.0);
        QVERIFY(assessment.reasons.join(QLatin1Char(' ')).contains(QStringLiteral("本地规则完整解析")));
    }

    void strongEntityEvidenceKeepsLocalResult()
    {
        auto result = resultFor(QStringLiteral("搜索红色牛仔裤"));
        result.assets.append(asset(0.88, 0.93,
                                   {QStringLiteral("关键词完整覆盖"),
                                    QStringLiteral("同一视觉对象属性已验证")}));
        const auto assessment = SearchReliabilityEvaluator::evaluate(result);

        QVERIFY(!assessment.shouldUseAssistant);
        QVERIFY(assessment.score >= 0.8);
        QCOMPARE(assessment.resultCount, qsizetype(1));
    }

    void zeroContentResultsTriggerAssistant()
    {
        const auto result = resultFor(QStringLiteral("搜索红色牛仔裤"));
        const auto assessment = SearchReliabilityEvaluator::evaluate(result);

        QVERIFY(assessment.shouldUseAssistant);
        QVERIFY(assessment.score < 0.62);
        QVERIFY(assessment.reasons.contains(QStringLiteral("本地内容搜索没有返回结果")));
    }

    void weakSemanticOnlyResultTriggersAssistant()
    {
        auto result = resultFor(QStringLiteral("未来感发布会现场"));
        result.assets.append(asset(0.38, 0.42, {QStringLiteral("本地语义命中")}));
        const auto assessment = SearchReliabilityEvaluator::evaluate(result);

        QVERIFY(assessment.shouldUseAssistant);
    }

    void exactFilenameDoesNotGetRewritten()
    {
        const auto result = resultFor(QStringLiteral("搜索 A001_C003.mp4"));
        const auto assessment = SearchReliabilityEvaluator::evaluate(result);

        QVERIFY(!assessment.shouldUseAssistant);
        QVERIFY(assessment.reasons.join(QLatin1Char(' ')).contains(QStringLiteral("精确文件")));
    }

    void complexRelationTriggersEvenWithPlausibleResult()
    {
        auto result = resultFor(QStringLiteral("不要夜景，找有汽车的素材"));
        result.assets.append(asset(0.82, 0.86, {QStringLiteral("关键词或视觉文本命中")}));
        const auto assessment = SearchReliabilityEvaluator::evaluate(result);

        QVERIFY(assessment.shouldUseAssistant);
        QVERIFY(assessment.reasons.join(QLatin1Char(' ')).contains(QStringLiteral("复杂关系")));
    }
};

QTEST_GUILESS_MAIN(SearchReliabilityEvaluatorTest)

#include "SearchReliabilityEvaluatorTest.moc"
