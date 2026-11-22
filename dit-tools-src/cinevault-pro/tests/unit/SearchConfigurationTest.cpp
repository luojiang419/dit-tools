#include "shared/SearchConfiguration.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtTest>

#ifndef CINEVAULT_LOCAL_SEARCH_DEPENDENCY_LOCK
#error "CINEVAULT_LOCAL_SEARCH_DEPENDENCY_LOCK must point to the machine-readable lock file"
#endif

class SearchConfigurationTest : public QObject {
    Q_OBJECT

private slots:
    void versions_areExplicitAndForwardOnly()
    {
        QCOMPARE(cinevault::searchconfig::kSearchIndexSchemaVersion, 1);
        QCOMPARE(cinevault::searchconfig::kStructuredVisionProfileVersion, 2);
    }

    void embeddingContract_isPinned()
    {
        QCOMPARE(cinevault::searchconfig::kEmbeddingModelId,
                 std::string_view("BAAI/bge-small-zh-v1.5"));
        QCOMPARE(cinevault::searchconfig::kEmbeddingDimensions, 512);
        QCOMPARE(cinevault::searchconfig::kEmbeddingMaxTokens, 512);
        QCOMPARE(cinevault::searchconfig::kEmbeddingArtifactRevision.size(), std::size_t(40));
    }

    void visualSampling_isFixedIntervalOnly()
    {
        QCOMPARE(cinevault::searchconfig::kSamplingPolicy,
                 std::string_view("fixed_interval"));
        QVERIFY(!cinevault::searchconfig::isValidFixedFrameInterval(0));
        QVERIFY(!cinevault::searchconfig::isValidFixedFrameInterval(-1));
        QVERIFY(cinevault::searchconfig::isValidFixedFrameInterval(1));
        QVERIFY(cinevault::searchconfig::isValidFixedFrameInterval(10));
    }

    void dependencyLock_matchesCompiledContract()
    {
        QFile lockFile(QStringLiteral(CINEVAULT_LOCAL_SEARCH_DEPENDENCY_LOCK));
        QVERIFY2(lockFile.open(QIODevice::ReadOnly), qPrintable(lockFile.errorString()));

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(lockFile.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(document.isObject());

        const auto root = document.object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);

        const auto configuration = root.value(QStringLiteral("searchConfiguration")).toObject();
        QCOMPARE(configuration.value(QStringLiteral("searchIndexSchemaVersion")).toInt(),
                 cinevault::searchconfig::kSearchIndexSchemaVersion);
        QCOMPARE(configuration.value(QStringLiteral("structuredVisionProfileVersion")).toInt(),
                 cinevault::searchconfig::kStructuredVisionProfileVersion);
        QCOMPARE(configuration.value(QStringLiteral("samplingPolicy")).toString(),
                 QString::fromUtf8(cinevault::searchconfig::kSamplingPolicy));
        QCOMPARE(configuration.value(QStringLiteral("embeddingModelId")).toString(),
                 QString::fromUtf8(cinevault::searchconfig::kEmbeddingModelId));
        QCOMPARE(configuration.value(QStringLiteral("embeddingDimensions")).toInt(),
                 cinevault::searchconfig::kEmbeddingDimensions);
        QCOMPARE(configuration.value(QStringLiteral("embeddingMaxTokens")).toInt(),
                 cinevault::searchconfig::kEmbeddingMaxTokens);

        const auto artifacts = root.value(QStringLiteral("artifacts")).toArray();
        QCOMPARE(artifacts.size(), 8);
        const QRegularExpression sha256Pattern(QStringLiteral("^[A-F0-9]{64}$"));
        for (const auto &artifactValue : artifacts) {
            const auto artifact = artifactValue.toObject();
            const auto id = artifact.value(QStringLiteral("id")).toString();
            const auto url = artifact.value(QStringLiteral("url")).toString();
            const auto installPath = artifact.value(QStringLiteral("installPath")).toString();
            const auto sha256 = artifact.value(QStringLiteral("sha256")).toString();
            QVERIFY2(!id.isEmpty(), "Every dependency artifact needs a stable id");
            QVERIFY2(url.startsWith(QStringLiteral("https://")), qPrintable(id));
            QVERIFY2(!url.contains(QStringLiteral("/main/")), qPrintable(id));
            QVERIFY2(!url.contains(QStringLiteral("/latest/")), qPrintable(id));
            QVERIFY2(!installPath.startsWith(QLatin1Char('/')), qPrintable(id));
            QVERIFY2(!installPath.contains(QStringLiteral("..")), qPrintable(id));
            QVERIFY2(artifact.value(QStringLiteral("size")).toInteger() > 0, qPrintable(id));
            QVERIFY2(sha256Pattern.match(sha256).hasMatch(), qPrintable(id));
        }
    }
};

QTEST_MAIN(SearchConfigurationTest)

#include "SearchConfigurationTest.moc"
