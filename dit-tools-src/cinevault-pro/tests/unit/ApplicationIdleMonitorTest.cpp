#include "application/ApplicationIdleMonitor.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>

class ApplicationIdleMonitorTest final : public QObject {
    Q_OBJECT

private slots:
    void becomesIdleAndResumesOnKeyboardInput();
    void explicitActivityRestartsTimeout();
};

void ApplicationIdleMonitorTest::becomesIdleAndResumesOnKeyboardInput()
{
    ApplicationIdleMonitor monitor;
    QSignalSpy idleSpy(&monitor, &ApplicationIdleMonitor::becameIdle);
    QSignalSpy resumedSpy(&monitor, &ApplicationIdleMonitor::activityResumed);

    monitor.start(30);
    QVERIFY(monitor.isActive());
    QTRY_COMPARE_WITH_TIMEOUT(idleSpy.count(), 1, 500);
    QVERIFY(monitor.isIdle());

    QObject receiver;
    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QCoreApplication::sendEvent(&receiver, &keyPress);
    QTRY_COMPARE_WITH_TIMEOUT(resumedSpy.count(), 1, 500);
    QVERIFY(!monitor.isIdle());
}

void ApplicationIdleMonitorTest::explicitActivityRestartsTimeout()
{
    ApplicationIdleMonitor monitor;
    QSignalSpy idleSpy(&monitor, &ApplicationIdleMonitor::becameIdle);

    monitor.start(300);
    QTest::qWait(100);
    monitor.recordActivity();
    QTest::qWait(220);
    QCOMPARE(idleSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(idleSpy.count(), 1, 500);

    monitor.stop();
    QVERIFY(!monitor.isActive());
}

QTEST_GUILESS_MAIN(ApplicationIdleMonitorTest)

#include "ApplicationIdleMonitorTest.moc"
