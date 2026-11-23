#include "shared/Formatters.h"

#include <QtTest>

class FormattersTest : public QObject {
    Q_OBJECT

private slots:
    void frameTimestamp_preservesZeroAndMilliseconds()
    {
        QCOMPARE(Formatters::formatFrameTimestamp(-1), QStringLiteral("未知时间"));
        QCOMPARE(Formatters::formatFrameTimestamp(0), QStringLiteral("0:00.000"));
        QCOMPARE(Formatters::formatFrameTimestamp(20750), QStringLiteral("0:20.750"));
        QCOMPARE(Formatters::formatFrameTimestamp(3600123), QStringLiteral("1:00:00.123"));
    }
};

QTEST_GUILESS_MAIN(FormattersTest)

#include "FormattersTest.moc"
