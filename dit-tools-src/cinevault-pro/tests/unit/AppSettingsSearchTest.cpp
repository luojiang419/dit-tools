#include "infrastructure/config/AppSettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class AppSettingsSearchTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsRoot.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("CineVaultTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AppSettingsSearchTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,
                           QSettings::UserScope,
                           m_settingsRoot.path());
    }

    void cleanup()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    void defaultsEnableLocalAssistantAndQuickSearch()
    {
        AppSettings settings;

        QVERIFY(settings.searchAssistantEnabled());
        QCOMPARE(settings.searchAssistantAutoUnloadMinutes(), 30);
        QVERIFY(settings.quickSearchEnabled());
        QCOMPARE(settings.quickSearchShortcut(), QStringLiteral("Alt+Space"));
        QVERIFY(!settings.hasQuickSearchWindowPosition());
        QVERIFY(!settings.startAtLogin());
        QCOMPARE(settings.closeButtonBehavior(), 0);
    }

    void searchAndQuickSearchSettingsPersist()
    {
        {
            AppSettings settings;
            settings.setSearchAssistantEnabled(false);
            settings.setSearchAssistantAutoUnloadMinutes(120);
            settings.setQuickSearchEnabled(false);
            settings.setQuickSearchShortcut(QStringLiteral("Ctrl+Shift+K"));
            settings.setQuickSearchWindowPosition(QPoint(-820, 135));
            settings.setStartAtLogin(true);
            settings.setCloseButtonBehavior(1);
            settings.sync();
        }

        AppSettings restored;
        QVERIFY(!restored.searchAssistantEnabled());
        QCOMPARE(restored.searchAssistantAutoUnloadMinutes(), 120);
        QVERIFY(!restored.quickSearchEnabled());
        QCOMPARE(restored.quickSearchShortcut(), QStringLiteral("Ctrl+Shift+K"));
        QVERIFY(restored.hasQuickSearchWindowPosition());
        QCOMPARE(restored.quickSearchWindowPosition(), QPoint(-820, 135));
        QVERIFY(restored.startAtLogin());
        QCOMPARE(restored.closeButtonBehavior(), 1);
    }

    void emptyShortcutFallsBackToDefault()
    {
        AppSettings settings;
        settings.setQuickSearchShortcut(QStringLiteral("   "));
        QCOMPARE(settings.quickSearchShortcut(), QStringLiteral("Alt+Space"));
    }

    void invalidCloseButtonBehaviorFallsBackToAsk()
    {
        QSettings rawSettings;
        rawSettings.setValue(QStringLiteral("ui/closeButtonBehavior"), 99);
        rawSettings.sync();

        AppSettings settings;
        QCOMPARE(settings.closeButtonBehavior(), 0);
        settings.setCloseButtonBehavior(2);
        QCOMPARE(settings.closeButtonBehavior(), 2);
    }

    void assistantAutoUnloadMinutesAreBounded()
    {
        AppSettings settings;
        settings.setSearchAssistantAutoUnloadMinutes(1);
        QCOMPARE(settings.searchAssistantAutoUnloadMinutes(), 5);
        settings.setSearchAssistantAutoUnloadMinutes(2000);
        QCOMPARE(settings.searchAssistantAutoUnloadMinutes(), 1440);

        QSettings rawSettings;
        rawSettings.setValue(QStringLiteral("materialCenter/searchAssistantAutoUnloadMinutes"), -20);
        rawSettings.sync();
        QCOMPARE(settings.searchAssistantAutoUnloadMinutes(), 5);
    }

    void customAnalysisDimensionsPersistAndNormalize()
    {
        {
            AppSettings settings;
            settings.setCustomAnalysisDimensions({
                QStringLiteral(" 电商转化潜力 "),
                QStringLiteral("服装版型"),
                QStringLiteral("电商转化潜力"),
                QStringLiteral("FASHION FIT"),
                QStringLiteral("fashion fit"),
                QString(33, QLatin1Char('x'))
            });
            settings.sync();
        }

        AppSettings restored;
        QCOMPARE(restored.customAnalysisDimensions(),
                 QStringList({QStringLiteral("电商转化潜力"),
                              QStringLiteral("服装版型"),
                              QStringLiteral("FASHION FIT")}));

        restored.setCustomAnalysisDimensions({});
        restored.sync();
        QVERIFY(restored.customAnalysisDimensions().isEmpty());
    }

private:
    QTemporaryDir m_settingsRoot;
};

QTEST_APPLESS_MAIN(AppSettingsSearchTest)

#include "AppSettingsSearchTest.moc"
