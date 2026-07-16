#include "core/search/NaturalLanguageQueryParser.h"
#include "core/search/SearchQueryUnderstanding.h"

#include <QJsonArray>
#include <QtTest>

#include <algorithm>

namespace {
QJsonObject validPayload()
{
    return QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("result_target"), QStringLiteral("frames")},
        {QStringLiteral("semantic_text"), QStringLiteral("穿蓝色牛仔长裤的人物")},
        {QStringLiteral("lexical_terms"), QJsonArray{QStringLiteral("人物"), QStringLiteral("长裤")}},
        {QStringLiteral("asset_types"), QJsonArray{QStringLiteral("video")}},
        {QStringLiteral("date"), QJsonObject{
            {QStringLiteral("start"), QString()}, {QStringLiteral("end"), QString()},
            {QStringLiteral("matched_text"), QString()}, {QStringLiteral("preferred_field"), QStringLiteral("any")}
        }},
        {QStringLiteral("folder_by_asset_criteria"), false},
        {QStringLiteral("ocr_text"), QString()},
        {QStringLiteral("entities"), QJsonArray{QJsonObject{
            {QStringLiteral("label"), QStringLiteral("裤子")},
            {QStringLiteral("colors"), QJsonArray{QStringLiteral("蓝色")}},
            {QStringLiteral("materials"), QJsonArray{QStringLiteral("牛仔")}},
            {QStringLiteral("attributes"), QJsonArray{QStringLiteral("长裤")}}
        }}},
        {QStringLiteral("confidence"), 0.91},
        {QStringLiteral("explanation"), QStringLiteral("用户在描述画面内容")}
    };
}

QJsonObject validV2Payload()
{
    const auto group = [](const QString &mode, const QStringList &alternatives) {
        QJsonArray values;
        for (const auto &value : alternatives) values.append(value);
        return QJsonObject{
            {QStringLiteral("mode"), mode},
            {QStringLiteral("alternatives"), values}
        };
    };
    return QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("result_target"), QStringLiteral("frames")},
        {QStringLiteral("semantic_variants"), QJsonArray{QJsonObject{
            {QStringLiteral("text"), QStringLiteral("女人穿红色牛仔裤")},
            {QStringLiteral("weight"), 0.9}
        }}},
        {QStringLiteral("lexical_groups"), QJsonArray{
            group(QStringLiteral("required"), {QStringLiteral("女人"), QStringLiteral("女性")}),
            group(QStringLiteral("required"), {QStringLiteral("牛仔裤"), QStringLiteral("丹宁裤")}),
            group(QStringLiteral("excluded"), {QStringLiteral("汽车")})
        }},
        {QStringLiteral("asset_types"), QJsonArray{}},
        {QStringLiteral("folder_by_asset_criteria"), false},
        {QStringLiteral("ocr_text"), QString()},
        {QStringLiteral("entities"), QJsonArray{
            QJsonObject{{QStringLiteral("label"), QStringLiteral("女人")},
                        {QStringLiteral("colors"), QJsonArray{}},
                        {QStringLiteral("materials"), QJsonArray{}},
                        {QStringLiteral("attributes"), QJsonArray{}}},
            QJsonObject{{QStringLiteral("label"), QStringLiteral("牛仔裤")},
                        {QStringLiteral("colors"), QJsonArray{QStringLiteral("红色")}},
                        {QStringLiteral("materials"), QJsonArray{}},
                        {QStringLiteral("attributes"), QJsonArray{}}}
        }},
        {QStringLiteral("cooccurrence"), QStringLiteral("same_frame")},
        {QStringLiteral("confidence"), 0.93},
        {QStringLiteral("ambiguities"), QJsonArray{}},
        {QStringLiteral("explanation"), QStringLiteral("拆分同帧实体和同义词")}
    };
}
}

class SearchQueryUnderstandingTest : public QObject {
    Q_OBJECT

private slots:
    void versionTwoSchemaFocusesOnLanguageUnderstanding()
    {
        const auto schema = SearchQueryUnderstanding::responseSchema();
        const auto properties = schema.value(QStringLiteral("properties")).toObject();
        QCOMPARE(properties.value(QStringLiteral("version")).toObject()
                     .value(QStringLiteral("const")).toInt(),
                 2);
        QVERIFY(properties.contains(QStringLiteral("semantic_variants")));
        QVERIFY(properties.contains(QStringLiteral("lexical_groups")));
        QVERIFY(properties.contains(QStringLiteral("cooccurrence")));
        QVERIFY(!properties.contains(QStringLiteral("date")));
    }

    void parsesStrictWhitelistedPayload()
    {
        QString error;
        const auto parsed = SearchQueryUnderstanding::parseModelPayload(validPayload(), &error);
        QVERIFY2(parsed.has_value(), qPrintable(error));
        QCOMPARE(parsed->resultTarget, SearchResultTarget::Frames);
        QCOMPARE(parsed->assetTypeFilters, QVector<int>{static_cast<int>(AssetType::Video)});
        QCOMPARE(parsed->strictEntities.size(), 1);
        QCOMPARE(parsed->strictEntities.first().colors, QStringList{QStringLiteral("蓝色")});
    }

    void parsesAndSafelyMergesVersionTwoPlan()
    {
        QString error;
        const auto parsed = SearchQueryUnderstanding::parseModelPayload(
            validV2Payload(),
            &error);
        QVERIFY2(parsed.has_value(), qPrintable(error));
        QCOMPARE(parsed->semanticVariants.size(), 1);
        QCOMPARE(parsed->lexicalGroups.size(), 3);
        QCOMPARE(parsed->cooccurrence, SearchCooccurrenceScope::SameFrame);

        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(
            QStringLiteral("穿红色牛仔裤的女人画面，但不要汽车"));
        bool applied = false;
        const auto merged = SearchQueryUnderstanding::merge(local, *parsed, &applied);

        QVERIFY(applied);
        QCOMPARE(merged.semanticVariants.size(), 1);
        QCOMPARE(merged.lexicalGroups.size(), 3);
        QVERIFY(merged.lexicalTerms.contains(QStringLiteral("丹宁裤")));
        QVERIFY(merged.excludedTerms.contains(QStringLiteral("汽车")));
        QCOMPARE(merged.cooccurrence, SearchCooccurrenceScope::SameFrame);
        QCOMPARE(merged.dateConstraint.startDate, local.dateConstraint.startDate);
        QCOMPARE(merged.dateConstraint.endDate, local.dateConstraint.endDate);
    }

    void localExplicitDateTypeAndTargetAlwaysWin()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("昨天拍摄的视频"), QDate(2026, 7, 14));
        auto payload = validPayload();
        payload.insert(QStringLiteral("date"), QJsonObject{
            {QStringLiteral("start"), QStringLiteral("2020-01-01")},
            {QStringLiteral("end"), QStringLiteral("2020-01-02")},
            {QStringLiteral("matched_text"), QStringLiteral("错误日期")},
            {QStringLiteral("preferred_field"), QStringLiteral("modified")}
        });
        QString error;
        const auto model = SearchQueryUnderstanding::parseModelPayload(payload, &error);
        QVERIFY2(model.has_value(), qPrintable(error));
        bool applied = false;
        const auto merged = SearchQueryUnderstanding::merge(local, *model, &applied);
        QVERIFY(applied);
        QCOMPARE(merged.resultTarget, SearchResultTarget::Assets);
        QCOMPARE(merged.assetTypeFilters, QVector<int>{static_cast<int>(AssetType::Video)});
        QCOMPARE(merged.dateConstraint.startDate, QStringLiteral("2026-07-13"));
        QCOMPARE(merged.dateConstraint.preferredField, SearchDateField::CapturedTime);
    }

    void modelCanEnrichVagueVisualQuery()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("找那个穿裤子的人"));
        QString error;
        const auto model = SearchQueryUnderstanding::parseModelPayload(validPayload(), &error);
        QVERIFY2(model.has_value(), qPrintable(error));
        bool applied = false;
        const auto merged = SearchQueryUnderstanding::merge(local, *model, &applied);
        QVERIFY(applied);
        QCOMPARE(merged.resultTarget, SearchResultTarget::Frames);
        QVERIFY(merged.lexicalTerms.contains(QStringLiteral("蓝色")));
        QVERIFY(merged.lexicalTerms.contains(QStringLiteral("牛仔")));
        QVERIFY(merged.interpretationLabels.contains(QStringLiteral("内置文本模型辅助理解")));
    }

    void mergeRemovesMaterialAlreadyImpliedByEntityLabel()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("蓝色牛仔裤"));
        ModelSearchUnderstanding model;
        model.confidence = 0.95;
        StrictEntityConstraint entity;
        entity.label = QStringLiteral("牛仔裤");
        entity.colors = {QStringLiteral("蓝色")};
        entity.materials = {QStringLiteral("牛仔")};
        model.strictEntities = {entity};

        const auto merged = SearchQueryUnderstanding::merge(local, model);

        QCOMPARE(merged.strictEntities.size(), 1);
        QCOMPARE(merged.strictEntities.first().label, QStringLiteral("牛仔裤"));
        QVERIFY(merged.strictEntities.first().materials.isEmpty());
        QVERIFY(merged.interpretationLabels.contains(QStringLiteral("同一对象：牛仔裤 蓝色")));
    }

    void modelAliasDoesNotBecomeSecondRequiredEntity()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("红色牛仔裤"));
        ModelSearchUnderstanding model;
        model.confidence = 0.95;
        model.lexicalTerms = {QStringLiteral("丹宁裤"), QStringLiteral("长裤")};
        StrictEntityConstraint alias;
        alias.label = QStringLiteral("裤子");
        alias.colors = {QStringLiteral("红色")};
        model.strictEntities = {alias};

        bool applied = false;
        const auto merged = SearchQueryUnderstanding::merge(local, model, &applied);

        QVERIFY(applied);
        QCOMPARE(merged.strictEntities.size(), 1);
        QCOMPARE(merged.strictEntities.first().label, QStringLiteral("牛仔裤"));
        QVERIFY(merged.lexicalTerms.contains(QStringLiteral("丹宁裤")));
        QVERIFY(merged.lexicalTerms.contains(QStringLiteral("裤子")));
    }

    void explicitSecondEntityBecomesRequiredCooccurrenceConstraint()
    {
        ParsedMaterialQuery local;
        local.originalText = QStringLiteral("有男人穿着红色牛仔裤的画面");
        StrictEntityConstraint jeans;
        jeans.label = QStringLiteral("牛仔裤");
        jeans.colors = {QStringLiteral("红色")};
        local.strictEntities = {jeans};

        ModelSearchUnderstanding model;
        model.confidence = 0.95;
        StrictEntityConstraint man;
        man.label = QStringLiteral("男人");
        model.strictEntities = {man, jeans};

        const auto merged = SearchQueryUnderstanding::merge(local, model);

        QCOMPARE(merged.strictEntities.size(), 2);
        QVERIFY(std::any_of(merged.strictEntities.cbegin(),
                            merged.strictEntities.cend(),
                            [](const auto &entity) {
            return entity.label == QStringLiteral("男人");
        }));
        QVERIFY(merged.interpretationLabels.join(QStringLiteral(" "))
                    .contains(QStringLiteral("男人")));
    }

    void modelHallucinatedPropertiesNeverBecomeHardConstraints()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("有男人穿着牛仔裤的画面"));

        ModelSearchUnderstanding model;
        model.confidence = 0.95;
        StrictEntityConstraint man;
        man.label = QStringLiteral("男人");
        man.attributes = {QStringLiteral("胡须"), QStringLiteral("穿着")};
        StrictEntityConstraint jeans;
        jeans.label = QStringLiteral("牛仔裤");
        jeans.colors = {QStringLiteral("红色")};
        model.strictEntities = {man, jeans};

        const auto merged = SearchQueryUnderstanding::merge(local, model);

        QCOMPARE(merged.strictEntities.size(), 2);
        for (const auto &entity : merged.strictEntities) {
            QVERIFY2(entity.colors.isEmpty(), "模型不得添加用户原句中不存在的颜色硬条件");
            QVERIFY2(entity.attributes.isEmpty(), "模型不得添加臆测属性或关系词硬条件");
        }
    }

    void hallucinatedDuskConstraintsNeverNarrowSemanticRecall()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("查找黄昏的画面"));

        ModelSearchUnderstanding model;
        model.confidence = 0.98;
        model.semanticText = QStringLiteral("夕阳落山时的金色天空和太阳");
        model.assetTypeFilters = {static_cast<int>(AssetType::Image)};
        model.dateConstraint.startDate = QStringLiteral("2026-07-14");
        model.dateConstraint.endDate = QStringLiteral("2026-07-14");
        model.dateConstraint.matchedText = QStringLiteral("昨天");
        model.ocrText = QStringLiteral("落日");
        StrictEntityConstraint sun;
        sun.label = QStringLiteral("太阳");
        sun.colors = {QStringLiteral("金色")};
        model.strictEntities = {sun};

        const auto merged = SearchQueryUnderstanding::merge(local, model);

        QCOMPARE(merged.semanticText, local.semanticText);
        QVERIFY(merged.assetTypeFilters.isEmpty());
        QVERIFY(merged.dateConstraint.isEmpty());
        QVERIFY(merged.ocrText.isEmpty());
        QVERIFY(merged.strictEntities.isEmpty());
    }

    void explicitWomanAndJeansRemainGroundedCooccurrenceConstraints()
    {
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(
            QStringLiteral("查找包含穿着红色牛仔裤的女人的画面"));

        ModelSearchUnderstanding model;
        model.confidence = 0.95;
        StrictEntityConstraint woman;
        woman.label = QStringLiteral("女性");
        StrictEntityConstraint jeans;
        jeans.label = QStringLiteral("牛仔裤");
        jeans.colors = {QStringLiteral("红色")};
        model.strictEntities = {woman, jeans};

        const auto merged = SearchQueryUnderstanding::merge(local, model);

        QCOMPARE(merged.strictEntities.size(), 2);
        QVERIFY(std::any_of(merged.strictEntities.cbegin(),
                            merged.strictEntities.cend(),
                            [](const auto &entity) {
            return entity.label == QStringLiteral("女性");
        }));
        QVERIFY(std::any_of(merged.strictEntities.cbegin(),
                            merged.strictEntities.cend(),
                            [](const auto &entity) {
            return entity.label == QStringLiteral("牛仔裤")
                && entity.colors.contains(QStringLiteral("红色"));
        }));
    }

    void rejectsInvalidDatesAndUnknownTypes()
    {
        auto invalidDate = validPayload();
        invalidDate.insert(QStringLiteral("date"), QJsonObject{
            {QStringLiteral("start"), QStringLiteral("2026-99-99")},
            {QStringLiteral("end"), QStringLiteral("2026-01-01")},
            {QStringLiteral("matched_text"), QStringLiteral("某天")},
            {QStringLiteral("preferred_field"), QStringLiteral("captured")}
        });
        QVERIFY(!SearchQueryUnderstanding::parseModelPayload(invalidDate).has_value());

        auto unknownType = validPayload();
        unknownType.insert(QStringLiteral("asset_types"), QJsonArray{QStringLiteral("executable")});
        QVERIFY(!SearchQueryUnderstanding::parseModelPayload(unknownType).has_value());
    }

    void lowConfidenceNeverChangesLocalQuery()
    {
        auto payload = validPayload();
        payload.insert(QStringLiteral("confidence"), 0.2);
        const auto model = SearchQueryUnderstanding::parseModelPayload(payload);
        QVERIFY(model.has_value());
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("昨天的图片"), QDate(2026, 7, 14));
        bool applied = true;
        const auto merged = SearchQueryUnderstanding::merge(local, *model, &applied);
        QVERIFY(!applied);
        QCOMPARE(merged.resultTarget, local.resultTarget);
        QCOMPARE(merged.dateConstraint.startDate, local.dateConstraint.startDate);
        QCOMPARE(merged.semanticText, local.semanticText);
    }

};

QTEST_GUILESS_MAIN(SearchQueryUnderstandingTest)

#include "SearchQueryUnderstandingTest.moc"
