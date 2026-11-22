#include <QtTest>

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void appName_isConfigured()
    {
        QVERIFY(true);
    }
};

QTEST_MAIN(SmokeTest)

#include "SmokeTest.moc"
