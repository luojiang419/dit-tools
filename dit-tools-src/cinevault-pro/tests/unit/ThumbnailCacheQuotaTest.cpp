#include "shared/Paths.h"
#include "shared/ThumbnailCacheQuota.h"

#include <QtTest>

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {
void writeSizedFile(const QString &path, qint64 sizeBytes, const QDateTime &accessedAt)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite | QIODevice::Truncate));
    QVERIFY(file.resize(sizeBytes));
    QVERIFY(file.setFileTime(accessedAt, QFileDevice::FileAccessTime));
    QVERIFY(file.setFileTime(accessedAt, QFileDevice::FileModificationTime));
}
}

class ThumbnailCacheQuotaTest : public QObject {
    Q_OBJECT

private slots:
    void lruEvictionIsBoundedToThumbnailCache()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto projectDatabase = QDir(temp.path()).filePath(QStringLiteral("project.cvdb"));
        const auto cacheRoot = Paths::projectThumbnailCacheRoot(projectDatabase);
        const auto oldest = QDir(cacheRoot).filePath(QStringLiteral("source_1/oldest.jpg"));
        const auto middle = QDir(cacheRoot).filePath(QStringLiteral("source_1/middle.jpg"));
        const auto newest = QDir(cacheRoot).filePath(QStringLiteral("source_1/newest.jpg"));
        const auto userFile = QDir(temp.path()).filePath(QStringLiteral("deliverables/master.mov"));
        const auto now = QDateTime::currentDateTime();
        writeSizedFile(oldest, 6, now.addDays(-3));
        writeSizedFile(middle, 6, now.addDays(-2));
        writeSizedFile(newest, 6, now.addDays(-1));
        writeSizedFile(userFile, 64, now.addDays(-10));

        const auto result = ThumbnailCacheQuota::enforceDirectory(cacheRoot, 10, 20);
        QCOMPARE(result.bytesBefore, qint64{18});
        QCOMPARE(result.bytesAfter, qint64{6});
        QCOMPARE(result.removedFiles, qint64{2});
        QVERIFY(!QFileInfo::exists(oldest));
        QVERIFY(!QFileInfo::exists(middle));
        QVERIFY(QFileInfo::exists(newest));
        QVERIFY(QFileInfo::exists(userFile));
        QVERIFY(result.withinHardLimit);

        ThumbnailCacheQuota::completeReferenceAuditForProject(projectDatabase);
        QVERIFY(!QFileInfo::exists(QDir(cacheRoot).filePath(
            QStringLiteral(".reference-audit-required"))));
    }

    void protectedFileAndHardLimitAreHonored()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto cacheRoot = QDir(temp.path()).filePath(QStringLiteral("cache/thumbnails"));
        const auto protectedFile = QDir(cacheRoot).filePath(QStringLiteral("protected.jpg"));
        const auto removableFile = QDir(cacheRoot).filePath(QStringLiteral("removable.jpg"));
        const auto now = QDateTime::currentDateTime();
        writeSizedFile(protectedFile, 8, now.addDays(-2));
        writeSizedFile(removableFile, 8, now.addDays(-1));

        const auto result = ThumbnailCacheQuota::enforceDirectory(
            cacheRoot, 8, 16, 0, {protectedFile});
        QVERIFY(QFileInfo::exists(protectedFile));
        QVERIFY(!QFileInfo::exists(removableFile));
        QCOMPARE(result.bytesAfter, qint64{8});

        const auto noRoom = ThumbnailCacheQuota::enforceDirectory(
            cacheRoot, 8, 16, 9, {protectedFile});
        QVERIFY(!noRoom.withinHardLimit);
        QVERIFY(QFileInfo::exists(protectedFile));
    }

    void projectLimitsNeverExceedContractCaps()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto limits = ThumbnailCacheQuota::limitsForProject(
            QDir(temp.path()).filePath(QStringLiteral("project.cvdb")));
        QVERIFY(limits.softLimitBytes >= 0);
        QVERIFY(limits.softLimitBytes <= ThumbnailCacheQuota::SoftLimitMaximumBytes);
        QVERIFY(limits.hardLimitBytes >= limits.softLimitBytes);
        QVERIFY(limits.hardLimitBytes <= ThumbnailCacheQuota::HardLimitMaximumBytes);
    }
};

QTEST_GUILESS_MAIN(ThumbnailCacheQuotaTest)

#include "ThumbnailCacheQuotaTest.moc"
