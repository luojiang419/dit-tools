#include "core/search/NaturalLanguageQueryParser.h"
#include "core/search/SearchQueryUnderstanding.h"
#include "infrastructure/search/LocalSearchAssistantRuntime.h"
#include "infrastructure/search/SearchAssistantClient.h"

#include <QElapsedTimer>
#include <QtTest>

#include <algorithm>

class SearchAssistantIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY2(m_runtime.assetsAvailable(),
                 qPrintable(QStringLiteral("运行时或模型缺失：%1 / %2")
                                .arg(m_runtime.executablePath(), m_runtime.modelPath())));
        QVERIFY2(m_runtime.start(), qPrintable(m_runtime.lastError()));
        QTRY_VERIFY_WITH_TIMEOUT(m_runtime.isReady(), 30000);
    }

    void missingAssetsFailClosed()
    {
        LocalSearchAssistantRuntime missing(
            QStringLiteral("Z:/cinevault-missing/llama-server.exe"),
            QStringLiteral("Z:/cinevault-missing/model.gguf"));

        QVERIFY(!missing.assetsAvailable());
        QVERIFY(!missing.start());
        QVERIFY(!missing.isReady());
        QVERIFY(missing.lastError().contains(QStringLiteral("资产不完整")));
    }

    void repeatedStartIsIdempotent()
    {
        const auto endpoint = m_runtime.endpoint();

        QVERIFY(m_runtime.start());
        QVERIFY(m_runtime.isReady());
        QCOMPARE(m_runtime.endpoint(), endpoint);
    }

    void usesGpuAccelerationWithoutCpuFallback()
    {
        QVERIFY(m_runtime.isReady());
        QVERIFY2(!m_runtime.gpuDeviceName().isEmpty(),
                 "内置文本模型必须检测到 GPU 后才允许进入就绪状态");
        qInfo() << "search assistant gpu=" << m_runtime.gpuDeviceName();
    }

    void unloadsAndReloadsGpuModel()
    {
        const auto originalGpu = m_runtime.gpuDeviceName();
        m_runtime.stop();
        QVERIFY(!m_runtime.isReady());
        QVERIFY(m_runtime.endpoint().isEmpty());

        QVERIFY2(m_runtime.start(), qPrintable(m_runtime.lastError()));
        QTRY_VERIFY_WITH_TIMEOUT(m_runtime.isReady(), 30000);
        QCOMPARE(m_runtime.gpuDeviceName(), originalGpu);
    }

    void understandsRedDenimJeans()
    {
        QElapsedTimer timer;
        timer.start();
        QString error;
        const auto result = m_client.understandQuery(
            QStringLiteral("搜索红色牛仔裤"),
            QDate(2026, 7, 15),
            m_runtime.endpoint(),
            m_runtime.modelName(),
            30,
            &error);

        QVERIFY2(result.has_value(), qPrintable(error));
        QVERIFY(result->confidence >= 0.55);
        QVERIFY(!result->semanticText.isEmpty());
        for (const auto &entity : result->strictEntities) {
            qInfo() << "red denim entity=" << entity.label
                    << entity.colors << entity.materials << entity.attributes;
        }
        QVERIFY(std::any_of(result->strictEntities.cbegin(),
                            result->strictEntities.cend(),
                            [](const auto &entity) {
            return entity.label.contains(QStringLiteral("牛仔裤"))
                && entity.colors.contains(QStringLiteral("红色"));
        }));
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("搜索红色牛仔裤"),
                                        QDate(2026, 7, 15));
        bool modelApplied = false;
        const auto merged = SearchQueryUnderstanding::merge(local, *result, &modelApplied);
        QVERIFY(modelApplied);
        QVERIFY(std::any_of(merged.lexicalTerms.cbegin(),
                            merged.lexicalTerms.cend(),
                            [&local](const QString &term) {
            return !local.lexicalTerms.contains(term, Qt::CaseInsensitive);
        }));
        qInfo() << "red denim semantic=" << result->semanticText
                << "lexical=" << result->lexicalTerms;
        qInfo() << "red denim query elapsed_ms=" << timer.elapsed();
    }

    void understandsTypeDateAndLocation()
    {
        QElapsedTimer timer;
        timer.start();
        QString error;
        const auto result = m_client.understandQuery(
            QStringLiteral("找上周上海拍的竖屏视频"),
            QDate(2026, 7, 15),
            m_runtime.endpoint(),
            m_runtime.modelName(),
            30,
            &error);

        QVERIFY2(result.has_value(), qPrintable(error));
        QVERIFY(result->confidence >= 0.55);
        NaturalLanguageQueryParser parser;
        const auto local = parser.parse(QStringLiteral("找上周上海拍的竖屏视频"),
                                        QDate(2026, 7, 15));
        const auto merged = SearchQueryUnderstanding::merge(local, *result);
        QVERIFY(merged.assetTypeFilters.contains(static_cast<int>(AssetType::Video)));
        QCOMPARE(merged.dateConstraint.startDate, QStringLiteral("2026-07-06"));
        QCOMPARE(merged.dateConstraint.endDate, QStringLiteral("2026-07-12"));
        QVERIFY(merged.semanticText.contains(QStringLiteral("上海"))
                || merged.lexicalTerms.contains(QStringLiteral("上海")));
        qInfo() << "type/date query elapsed_ms=" << timer.elapsed();
    }

    void understandsPictureAsFramesAndKeepsBothRequiredEntities()
    {
        QString error;
        const auto result = m_client.understandQuery(
            QStringLiteral("有男人穿着牛仔裤的画面"),
            QDate(2026, 7, 15),
            m_runtime.endpoint(),
            m_runtime.modelName(),
            30,
            &error);

        QVERIFY2(result.has_value(), qPrintable(error));
        QVERIFY(result->confidence >= 0.55);
        QCOMPARE(result->resultTarget, SearchResultTarget::Frames);
        for (const auto &entity : result->strictEntities) {
            qInfo() << "man jeans entity=" << entity.label
                    << entity.colors << entity.materials << entity.attributes;
        }
        const auto hasMan = std::any_of(
            result->strictEntities.cbegin(), result->strictEntities.cend(), [](const auto &entity) {
                return entity.label == QStringLiteral("男人")
                    || entity.label == QStringLiteral("男性")
                    || entity.label == QStringLiteral("男子");
            });
        const auto hasJeans = std::any_of(
            result->strictEntities.cbegin(), result->strictEntities.cend(), [](const auto &entity) {
                return entity.label.contains(QStringLiteral("牛仔裤"))
                    || (entity.label.contains(QStringLiteral("裤"))
                        && entity.materials.contains(QStringLiteral("牛仔")));
            });
        QVERIFY(hasMan);
        QVERIFY(hasJeans);
    }

    void threeRealQueriesRemainValidForTwentyRounds()
    {
        const QStringList queries{
            QStringLiteral("搜索红色牛仔裤"),
            QStringLiteral("找上周上海拍的竖屏视频"),
            QStringLiteral("有男人穿着牛仔裤的画面")
        };
        for (int round = 1; round <= 20; ++round) {
            for (const auto &query : queries) {
                QString error;
                const auto result = m_client.understandQuery(
                    query,
                    QDate(2026, 7, 15),
                    m_runtime.endpoint(),
                    m_runtime.modelName(),
                    30,
                    &error);
                QVERIFY2(result.has_value(),
                         qPrintable(QStringLiteral("第 %1 轮查询失败（%2）：%3")
                                        .arg(round)
                                        .arg(query, error)));
                QCOMPARE(result->protocolVersion, 2);
                QVERIFY(result->confidence >= 0.55);
            }
        }
    }

    void cleanupTestCase()
    {
        m_runtime.stop();
        QVERIFY(!m_runtime.isReady());
        QVERIFY(m_runtime.endpoint().isEmpty());
    }

private:
    LocalSearchAssistantRuntime m_runtime;
    SearchAssistantClient m_client;
};

QTEST_GUILESS_MAIN(SearchAssistantIntegrationTest)

#include "SearchAssistantIntegrationTest.moc"
