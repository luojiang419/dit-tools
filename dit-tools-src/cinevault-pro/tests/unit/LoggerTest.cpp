#include "infrastructure/logging/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureSynchronizer>
#include <QTemporaryDir>
#include <QtConcurrentRun>
#include <QtTest>

class LoggerTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        Logger::shutdown();
    }

    void concurrentWritesRemainComplete()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto logPath = tempDir.filePath(QStringLiteral("concurrent.log"));
        QString errorMessage;
        QVERIFY2(Logger::initialize(logPath, &errorMessage), qPrintable(errorMessage));

        constexpr int threadCount = 8;
        constexpr int messagesPerThread = 100;
        QFutureSynchronizer<void> synchronizer;
        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
            synchronizer.addFuture(QtConcurrent::run([threadIndex]() {
                for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex) {
                    Logger::info(QStringLiteral("[thread-%1-message-%2]")
                                     .arg(threadIndex)
                                     .arg(messageIndex));
                }
            }));
        }
        synchronizer.waitForFinished();
        Logger::warn(QStringLiteral("[flush-marker]"));

        QFile logFile(logPath);
        QVERIFY(logFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const auto content = logFile.readAll();
        QCOMPARE(content.count("[thread-"), threadCount * messagesPerThread);
        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
            for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex) {
                const auto marker = QStringLiteral("[thread-%1-message-%2]")
                                        .arg(threadIndex)
                                        .arg(messageIndex)
                                        .toUtf8();
                QCOMPARE(content.count(marker), 1);
            }
        }
    }

    void rotatesAtBoundedSizeAndKeepsThreeBackups()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const auto logPath = tempDir.filePath(QStringLiteral("rotation.log"));
        QString errorMessage;
        QVERIFY2(Logger::initialize(logPath, &errorMessage), qPrintable(errorMessage));

        const QString payload(256 * 1024, QLatin1Char('x'));
        for (int index = 0; index < 84; ++index) {
            Logger::info(QStringLiteral("[%1]%2").arg(index).arg(payload));
        }
        Logger::warn(QStringLiteral("[rotation-flush-marker]"));

        constexpr qint64 maxLogFileBytes = 5 * 1024 * 1024;
        QVERIFY(QFileInfo::exists(logPath));
        QVERIFY(QFileInfo::exists(logPath + QStringLiteral(".1")));
        QVERIFY(QFileInfo::exists(logPath + QStringLiteral(".2")));
        QVERIFY(QFileInfo::exists(logPath + QStringLiteral(".3")));
        QVERIFY(QFileInfo(logPath).size() <= maxLogFileBytes);
        for (int index = 1; index <= 3; ++index) {
            QVERIFY(QFileInfo(logPath + QStringLiteral(".%1").arg(index)).size()
                    <= maxLogFileBytes);
        }
        QVERIFY(!QFileInfo::exists(logPath + QStringLiteral(".4")));
    }
};

QTEST_GUILESS_MAIN(LoggerTest)

#include "LoggerTest.moc"
