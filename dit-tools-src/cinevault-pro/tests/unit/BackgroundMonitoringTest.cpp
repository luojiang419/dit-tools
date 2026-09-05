#include "application/SourceChangeMonitor.h"
#include "application/SystemIdleMonitor.h"
#include "core/scan/ScanPathPolicy.h"

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStorageInfo>
#include <QTemporaryDir>

class BackgroundMonitoringTest : public QObject {
    Q_OBJECT

private slots:
    void systemIdleMonitorUsesSystemThresholdAndResumes()
    {
        qint64 idleDuration = 0;
        SystemIdleMonitor monitor;
        monitor.setIdleDurationProviderForTesting([&idleDuration]() { return idleDuration; });
        QSignalSpy becameIdle(&monitor, &SystemIdleMonitor::becameIdle);
        QSignalSpy activityResumed(&monitor, &SystemIdleMonitor::activityResumed);

        monitor.start(1000, 60000);
        QVERIFY(monitor.isActive());
        QVERIFY(!monitor.isIdle());

        idleDuration = 999;
        monitor.pollNow();
        QCOMPARE(becameIdle.count(), 0);

        idleDuration = 1000;
        monitor.pollNow();
        QVERIFY(monitor.isIdle());
        QCOMPARE(becameIdle.count(), 1);

        idleDuration = 5000;
        monitor.pollNow();
        QCOMPARE(becameIdle.count(), 1);

        idleDuration = 20;
        monitor.pollNow();
        QVERIFY(!monitor.isIdle());
        QCOMPARE(activityResumed.count(), 1);
        monitor.stop();
        QVERIFY(!monitor.isActive());
    }

    void sourceMonitorDetectsRecursiveFileChanges()
    {
#ifndef Q_OS_WIN
        QSKIP("递归目录通知契约当前针对 Windows 桌面版。");
#else
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto nestedPath = QDir(temp.path()).filePath(QStringLiteral("nested/deep"));
        QVERIFY(QDir().mkpath(nestedPath));

        SourceRoot sourceRoot;
        sourceRoot.id = 42;
        sourceRoot.name = QStringLiteral("Monitor Test");
        sourceRoot.path = temp.path();

        SourceChangeMonitor monitor;
        QSignalSpy changed(&monitor, &SourceChangeMonitor::sourceChanged);
        QSignalSpy batches(&monitor, &SourceChangeMonitor::sourceChangesDetected);
        QSignalSpy unavailable(&monitor, &SourceChangeMonitor::sourceUnavailable);
        monitor.setSourceRoots({sourceRoot});
        QCOMPARE(monitor.watchedSourceCount(), 1);
        QTest::qWait(250);
        QCOMPARE(unavailable.count(), 0);

        QFile file(QDir(nestedPath).filePath(QStringLiteral("new-file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("recursive change"), qint64{16});
        file.close();

        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(batches.count(), 1, 5000);
        QCOMPARE(changed.first().at(0).toLongLong(), qint64{42});
        QCOMPARE(QDir::cleanPath(changed.first().at(1).toString()),
                 QDir::cleanPath(temp.path()));
        const auto batch = qvariant_cast<SourceChangeBatch>(batches.first().at(0));
        QCOMPARE(batch.sourceRootId, qint64{42});
        QVERIFY(!batch.overflowed);
        QVERIFY(batch.changedPaths.contains(QFileInfo(file).absoluteFilePath()));
        QVERIFY(batch.changedPaths.contains(QFileInfo(file).absolutePath()));
        monitor.stop();
        QCOMPARE(monitor.watchedSourceCount(), 0);
#endif
    }

    void scanPathPolicyMergesDirectoriesAndProtectsGeneratedData()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto sourcePath = QDir(temp.path()).filePath(QStringLiteral("source"));
        const auto deepPath = QDir(sourcePath).filePath(QStringLiteral("A/deep"));
        const auto projectPath = QDir(sourcePath).filePath(QStringLiteral("project"));
        QVERIFY(QDir().mkpath(deepPath));
        QVERIFY(QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("cache/thumbnails"))));

        const auto deepFile = QDir(deepPath).filePath(QStringLiteral("changed.raw"));
        const auto rootFile = QDir(sourcePath).filePath(QStringLiteral("root.mov"));
        QFile(deepFile).open(QIODevice::WriteOnly);
        QFile(rootFile).open(QIODevice::WriteOnly);
        const auto projectDatabase = QDir(projectPath).filePath(QStringLiteral("catalog.cvdb"));
        const auto generatedThumbnail = QDir(projectPath)
                                            .filePath(QStringLiteral("cache/thumbnails/1.jpg"));

        const auto directories = ScanPathPolicy::normalizeDirtyDirectories(
            sourcePath,
            {deepFile, rootFile, generatedThumbnail},
            projectDatabase);
        QVERIFY(directories.contains(QFileInfo(deepFile).absolutePath()));
        QVERIFY(directories.contains(QFileInfo(rootFile).absolutePath()));
        QVERIFY(!directories.contains(QFileInfo(generatedThumbnail).absolutePath()));
        QVERIFY(ScanPathPolicy::pathsOverlap(sourcePath, deepPath));
        QVERIFY(!ScanPathPolicy::pathsOverlap(sourcePath,
                                              QDir(temp.path()).filePath(QStringLiteral("other"))));

        const auto volumeRoot = QStorageInfo(temp.path()).rootPath();
        QVERIFY(ScanPathPolicy::isPathInside(
            QDir(volumeRoot).filePath(QStringLiteral("DCIM")), volumeRoot));
        QVERIFY(ScanPathPolicy::isExcludedPath(
            volumeRoot,
            QDir(volumeRoot).filePath(QStringLiteral("System Volume Information/shadow.dat")),
            QString()));
    }

    void sourceMonitorBoundsPendingPathsAndMarksOverflow()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QStringList changedPaths;
        changedPaths.reserve(4097);
        for (int index = 0; index < 4097; ++index) {
            changedPaths.append(QDir(temp.path()).filePath(
                QStringLiteral("change-%1.raw").arg(index)));
        }

        SourceChangeMonitor monitor;
        QSignalSpy batches(&monitor, &SourceChangeMonitor::sourceChangesDetected);
        monitor.recordChangesForTesting(99, temp.path(), changedPaths);
        QTRY_COMPARE_WITH_TIMEOUT(batches.count(), 1, 5000);
        const auto batch = qvariant_cast<SourceChangeBatch>(batches.first().at(0));
        QCOMPARE(batch.sourceRootId, qint64{99});
        QVERIFY(batch.overflowed);
        QVERIFY(batch.changedPaths.isEmpty());
    }

    void sourceMonitorKeepsChangesBelowDriveRoot()
    {
#ifndef Q_OS_WIN
        QSKIP("盘符根路径契约仅适用于 Windows。");
#else
        SourceChangeMonitor monitor;
        QSignalSpy batches(&monitor, &SourceChangeMonitor::sourceChangesDetected);
        monitor.recordChangesForTesting(
            7,
            QStringLiteral("G:/"),
            {QStringLiteral("G:/DCIM/sample.mov")});

        QTRY_COMPARE_WITH_TIMEOUT(batches.count(), 1, 5000);
        const auto batch = qvariant_cast<SourceChangeBatch>(batches.first().at(0));
        QCOMPARE(batch.sourceRootId, qint64{7});
        QVERIFY(!batch.overflowed);
        QVERIFY(batch.changedPaths.contains(QStringLiteral("G:/DCIM/sample.mov")));
        QVERIFY(batch.changedPaths.contains(QStringLiteral("G:/DCIM")));
#endif
    }
};

QTEST_GUILESS_MAIN(BackgroundMonitoringTest)

#include "BackgroundMonitoringTest.moc"
