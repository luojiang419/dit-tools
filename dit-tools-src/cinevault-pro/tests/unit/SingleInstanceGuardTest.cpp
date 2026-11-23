#include "app/SingleInstanceGuard.h"

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

class SingleInstanceGuardTest final : public QObject {
    Q_OBJECT

private slots:
    void secondLaunchNotifiesPrimaryInstance();
    void lockIsReleasedWhenPrimaryExits();
};

void SingleInstanceGuardTest::secondLaunchNotifiesPrimaryInstance()
{
    const auto applicationId = QStringLiteral("cinevault-test-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    SingleInstanceGuard primary(applicationId);
    QString errorMessage;
    QCOMPARE(primary.start(&errorMessage),
             SingleInstanceGuard::StartResult::PrimaryInstance);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(primary.isPrimary());

    QSignalSpy activationSpy(&primary, &SingleInstanceGuard::activationRequested);
    SingleInstanceGuard secondary(applicationId);
    QCOMPARE(secondary.start(&errorMessage),
             SingleInstanceGuard::StartResult::SecondaryInstanceNotified);
    QVERIFY(!secondary.isPrimary());
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.count(), 1, 2000);

    QCOMPARE(primary.start(&errorMessage),
             SingleInstanceGuard::StartResult::PrimaryInstance);
}

void SingleInstanceGuardTest::lockIsReleasedWhenPrimaryExits()
{
    const auto applicationId = QStringLiteral("cinevault-test-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        SingleInstanceGuard primary(applicationId);
        QCOMPARE(primary.start(), SingleInstanceGuard::StartResult::PrimaryInstance);
    }

    SingleInstanceGuard replacement(applicationId);
    QCOMPARE(replacement.start(), SingleInstanceGuard::StartResult::PrimaryInstance);
    QVERIFY(replacement.isPrimary());
}

QTEST_GUILESS_MAIN(SingleInstanceGuardTest)

#include "SingleInstanceGuardTest.moc"
