#include "infrastructure/config/AppSettings.h"
#include "ui/window/QuickSearchController.h"

#include <QApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class QuickSearchControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsRoot.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("CineVaultTests"));
        QCoreApplication::setApplicationName(QStringLiteral("QuickSearchControllerTest"));
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

    void normalizesDefaultAndRecordedShortcuts()
    {
        QCOMPARE(QuickSearchController::normalizedShortcut(QStringLiteral("Alt+Space")),
                 QStringLiteral("Alt+Space"));
        QCOMPARE(QuickSearchController::shortcutFromKeyEvent(Qt::Key_K,
                                                             Qt::ControlModifier | Qt::ShiftModifier),
                 QStringLiteral("Ctrl+Shift+K"));
        QVERIFY(QuickSearchController::normalizedShortcut(QStringLiteral("Space")).isEmpty());
    }

    void registersIsolatedHotkeyAndDispatchesActivation()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows global hotkey integration test");
#else
        AppSettings settings;
        settings.setQuickSearchEnabled(false);
        QuickSearchController controller(&settings);

        QString errorMessage;
        QVERIFY2(controller.applyShortcutConfiguration(true,
                                                       QStringLiteral("Ctrl+Shift+F12"),
                                                       &errorMessage),
                 qPrintable(errorMessage));
        QCOMPARE(controller.shortcut(), QStringLiteral("Ctrl+Shift+F12"));
        QVERIFY(controller.shortcutStatusText().contains(QStringLiteral("已启用")));

        QSignalSpy activationSpy(&controller, &QuickSearchController::quickSearchRequested);
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 0x4356, 0));
        QTRY_COMPARE_WITH_TIMEOUT(activationSpy.count(), 1, 2000);
#endif
    }

    void clampsRememberedPositionAcrossMultipleScreens()
    {
        const QList<QRect> screens{
            QRect(0, 0, 1920, 1080),
            QRect(1920, 0, 1280, 1024)
        };
        QCOMPARE(QuickSearchController::clampWindowPosition(QPoint(4000, 2000),
                                                            QSize(820, 650),
                                                            screens,
                                                            QPoint(2000, 100)),
                 QPoint(2380, 374));
        QCOMPARE(QuickSearchController::clampWindowPosition(QPoint(2100, 100),
                                                            QSize(820, 650),
                                                            screens,
                                                            QPoint(100, 100)),
                 QPoint(2100, 100));
    }

    void restoresHiddenMainWindowThroughNativeController()
    {
        AppSettings settings;
        settings.setQuickSearchEnabled(false);
        QuickSearchController controller(&settings);
        QObject invalidWindow;
        QVERIFY(!controller.restoreMainWindow(nullptr));
        QVERIFY(!controller.restoreMainWindow(&invalidWindow));

        QWindow mainWindow;
        mainWindow.resize(1, 1);
        mainWindow.setPosition(-32000, -32000);
        mainWindow.hide();
        QVERIFY(!mainWindow.isVisible());

        QVERIFY(controller.restoreMainWindow(&mainWindow));
        QTRY_VERIFY(mainWindow.isVisible());
        QVERIFY(!(mainWindow.windowState() & Qt::WindowMinimized));
#ifdef Q_OS_WIN
        const auto windowHandle = reinterpret_cast<HWND>(mainWindow.winId());
        QVERIFY(IsWindowVisible(windowHandle));
        QTRY_VERIFY_WITH_TIMEOUT(controller.isMainWindowForeground(&mainWindow), 2000);
        QCOMPARE(GetForegroundWindow(), windowHandle);
#else
        QTRY_VERIFY_WITH_TIMEOUT(controller.isMainWindowForeground(&mainWindow), 2000);
#endif
        mainWindow.hide();
    }

    void retriesUntilHiddenMainWindowBecomesForeground()
    {
        AppSettings settings;
        settings.setQuickSearchEnabled(false);
        QuickSearchController controller(&settings);
        QSignalSpy restoreSpy(&controller, &QuickSearchController::mainWindowRestoreFinished);

        QWindow mainWindow;
        mainWindow.resize(1, 1);
        mainWindow.setPosition(-32000, -32000);
        mainWindow.hide();

        QWindow competingWindow;
        competingWindow.resize(1, 1);
        competingWindow.setPosition(-32000, -32000);
        competingWindow.show();

        QVERIFY(controller.restoreMainWindow(&mainWindow));
#ifdef Q_OS_WIN
        const auto competingHandle = reinterpret_cast<HWND>(competingWindow.winId());
        ShowWindow(competingHandle, SW_SHOW);
        QVERIFY(SetForegroundWindow(competingHandle));
        QCOMPARE(GetForegroundWindow(), competingHandle);
#else
        competingWindow.raise();
        competingWindow.requestActivate();
#endif
        QTRY_VERIFY_WITH_TIMEOUT(!restoreSpy.isEmpty(), 2000);
        QCOMPARE(restoreSpy.constLast().constFirst().toBool(), true);
        QVERIFY(controller.isMainWindowForeground(&mainWindow));
        competingWindow.hide();
        mainWindow.hide();
    }

private:
    QTemporaryDir m_settingsRoot;
};

QTEST_MAIN(QuickSearchControllerTest)

#include "QuickSearchControllerTest.moc"
