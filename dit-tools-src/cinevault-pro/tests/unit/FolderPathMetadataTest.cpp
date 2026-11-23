#include "shared/FolderPathMetadata.h"

#include <QtTest>

class FolderPathMetadataTest : public QObject {
    Q_OBJECT

private slots:
    void normalizedPathKey_preservesUncRootAndNormalizesSeparators()
    {
        QCOMPARE(FolderPathMetadata::normalizedPathKey(QStringLiteral(R"(\\Server\Share\Shoot\DAY01\)")),
                 QStringLiteral("//server/share/shoot/day01"));
        QCOMPARE(FolderPathMetadata::relativePathFromRoot(QStringLiteral(R"(\\Server\Share\Shoot)"),
                                                          QStringLiteral(R"(\\server\share\Shoot\2026-07-14\CameraA)")),
                 QStringLiteral("2026-07-14/CameraA"));
        QCOMPARE(FolderPathMetadata::parentRelativePath(QStringLiteral(R"(2026-07-14\CameraA)")),
                 QStringLiteral("2026-07-14"));
        QCOMPARE(FolderPathMetadata::depth(QStringLiteral(R"(2026-07-14\CameraA)")), 2);
    }

    void normalizeSourcePath_acceptsUncAndFileUrls()
    {
        QCOMPARE(FolderPathMetadata::normalizeSourcePath(QStringLiteral(R"(  \\Server\Share\Shoot\DAY01\  )")),
                 QStringLiteral("//Server/Share/Shoot/DAY01"));
        QCOMPARE(FolderPathMetadata::normalizeSourcePath(QStringLiteral("file://Server/Share/Shoot/DAY01")),
                 QStringLiteral("//server/Share/Shoot/DAY01"));
        QCOMPARE(FolderPathMetadata::normalizeSourcePath(QStringLiteral("C:\\Shoot\\DAY01\\..\\DAY02")),
                 QStringLiteral("C:/Shoot/DAY02"));
    }

    void inferDate_acceptsSupportedFormatsAndRejectsInvalidDates_data()
    {
        QTest::addColumn<QString>("rootName");
        QTest::addColumn<QString>("relativePath");
        QTest::addColumn<QString>("expectedDate");
        QTest::addColumn<QString>("expectedAnchor");

        QTest::newRow("hyphen") << QStringLiteral("Card") << QStringLiteral("2026-07-14/CameraA")
                                 << QStringLiteral("2026-07-14") << QStringLiteral("2026-07-14");
        QTest::newRow("compact inherited") << QStringLiteral("Card") << QStringLiteral("20260714/CameraA/Proxy")
                                            << QStringLiteral("2026-07-14") << QStringLiteral("20260714");
        QTest::newRow("chinese") << QStringLiteral("Card") << QStringLiteral("2026年7月14日/录音")
                                  << QStringLiteral("2026-07-14") << QStringLiteral("2026年7月14日");
        QTest::newRow("nearest ancestor") << QStringLiteral("2025-01-01") << QStringLiteral("2026.07.14/CameraA")
                                           << QStringLiteral("2026-07-14") << QStringLiteral("2026.07.14");
        QTest::newRow("root anchor") << QStringLiteral("Shoot_2026_07_14") << QString()
                                      << QStringLiteral("2026-07-14") << QStringLiteral(".");
        QTest::newRow("invalid") << QStringLiteral("Card") << QStringLiteral("2026-02-30/CameraA")
                                  << QString() << QString();
    }

    void inferDate_acceptsSupportedFormatsAndRejectsInvalidDates()
    {
        QFETCH(QString, rootName);
        QFETCH(QString, relativePath);
        QFETCH(QString, expectedDate);
        QFETCH(QString, expectedAnchor);

        const auto result = FolderPathMetadata::inferDate(rootName, relativePath);
        QCOMPARE(result.normalizedDate, expectedDate);
        QCOMPARE(result.anchorRelativePath, expectedAnchor);
    }

    void ancestorRelativePaths_includesRoot()
    {
        QCOMPARE(FolderPathMetadata::ancestorRelativePaths(QStringLiteral("A/B")),
                 QStringList({QStringLiteral("A/B"), QStringLiteral("A"), QString()}));
    }
};

QTEST_GUILESS_MAIN(FolderPathMetadataTest)

#include "FolderPathMetadataTest.moc"
