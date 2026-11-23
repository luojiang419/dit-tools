#include "ui/window/QuickSearchController.h"

#include "infrastructure/config/AppSettings.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyCombination>
#include <QKeySequence>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWindow>

#include <array>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int kQuickSearchHotKeyId = 0x4356;
constexpr auto kRunRegistryPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kRunRegistryValue = "CineVault";
constexpr std::array<int, 4> kMainWindowRestoreRetryDelaysMs{0, 50, 150, 300};

bool isModifierKey(Qt::Key key)
{
    return key == Qt::Key_Control
        || key == Qt::Key_Shift
        || key == Qt::Key_Alt
        || key == Qt::Key_Meta
        || key == Qt::Key_AltGr;
}

#ifdef Q_OS_WIN
bool nativeShortcut(const QKeyCombination &combination,
                    unsigned int *nativeModifiers,
                    unsigned int *nativeVirtualKey)
{
    if (!nativeModifiers || !nativeVirtualKey) {
        return false;
    }

    unsigned int modifiers = MOD_NOREPEAT;
    const auto qtModifiers = combination.keyboardModifiers();
    if (qtModifiers.testFlag(Qt::AltModifier)) modifiers |= MOD_ALT;
    if (qtModifiers.testFlag(Qt::ControlModifier)) modifiers |= MOD_CONTROL;
    if (qtModifiers.testFlag(Qt::ShiftModifier)) modifiers |= MOD_SHIFT;
    if (qtModifiers.testFlag(Qt::MetaModifier)) modifiers |= MOD_WIN;

    const auto key = combination.key();
    unsigned int virtualKey = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        virtualKey = static_cast<unsigned int>('A' + key - Qt::Key_A);
    } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        virtualKey = static_cast<unsigned int>('0' + key - Qt::Key_0);
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        virtualKey = static_cast<unsigned int>(VK_F1 + key - Qt::Key_F1);
    } else {
        switch (key) {
        case Qt::Key_Space: virtualKey = VK_SPACE; break;
        case Qt::Key_Tab: virtualKey = VK_TAB; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: virtualKey = VK_RETURN; break;
        case Qt::Key_Escape: virtualKey = VK_ESCAPE; break;
        case Qt::Key_Backspace: virtualKey = VK_BACK; break;
        case Qt::Key_Insert: virtualKey = VK_INSERT; break;
        case Qt::Key_Delete: virtualKey = VK_DELETE; break;
        case Qt::Key_Home: virtualKey = VK_HOME; break;
        case Qt::Key_End: virtualKey = VK_END; break;
        case Qt::Key_PageUp: virtualKey = VK_PRIOR; break;
        case Qt::Key_PageDown: virtualKey = VK_NEXT; break;
        case Qt::Key_Left: virtualKey = VK_LEFT; break;
        case Qt::Key_Up: virtualKey = VK_UP; break;
        case Qt::Key_Right: virtualKey = VK_RIGHT; break;
        case Qt::Key_Down: virtualKey = VK_DOWN; break;
        default:
            if (key > 0 && key <= 0xffff) {
                const auto translated = VkKeyScanW(static_cast<wchar_t>(key));
                if (translated != -1) {
                    virtualKey = LOBYTE(translated);
                    const auto translatedModifiers = HIBYTE(translated);
                    if (translatedModifiers & 1) modifiers |= MOD_SHIFT;
                    if (translatedModifiers & 2) modifiers |= MOD_CONTROL;
                    if (translatedModifiers & 4) modifiers |= MOD_ALT;
                }
            }
            break;
        }
    }

    if (virtualKey == 0) {
        return false;
    }
    *nativeModifiers = modifiers;
    *nativeVirtualKey = virtualKey;
    return true;
}

bool restoreNativeWindowToForeground(HWND windowHandle)
{
    if (!windowHandle || !IsWindow(windowHandle)) {
        return false;
    }

    ShowWindow(windowHandle, IsIconic(windowHandle) ? SW_RESTORE : SW_SHOW);

    const auto foregroundWindow = GetForegroundWindow();
    const auto windowThreadId = GetWindowThreadProcessId(windowHandle, nullptr);
    const auto foregroundThreadId = foregroundWindow
        ? GetWindowThreadProcessId(foregroundWindow, nullptr)
        : 0;
    const bool attachedToForeground = foregroundThreadId != 0
        && windowThreadId != 0
        && foregroundThreadId != windowThreadId
        && AttachThreadInput(windowThreadId, foregroundThreadId, TRUE) != FALSE;

    SetWindowPos(windowHandle,
                 HWND_TOP,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(windowHandle);
    SetActiveWindow(windowHandle);
    SetFocus(windowHandle);
    SetForegroundWindow(windowHandle);

    if (GetForegroundWindow() != windowHandle) {
        SetWindowPos(windowHandle,
                     HWND_TOPMOST,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(windowHandle,
                     HWND_NOTOPMOST,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(windowHandle);
    }

    if (attachedToForeground) {
        AttachThreadInput(windowThreadId, foregroundThreadId, FALSE);
    }
    return GetForegroundWindow() == windowHandle;
}

bool isNativeWindowForeground(HWND windowHandle)
{
    return windowHandle
        && IsWindow(windowHandle)
        && GetForegroundWindow() == windowHandle;
}
#endif
}

QuickSearchController::QuickSearchController(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_startHidden = QCoreApplication::arguments().contains(QStringLiteral("--background"),
                                                            Qt::CaseInsensitive);
    if (qApp) {
        qApp->installNativeEventFilter(this);
    }
    createTrayIcon();

    QString errorMessage;
    const auto enabled = m_settings && m_settings->quickSearchEnabled();
    const auto shortcut = m_settings
        ? m_settings->quickSearchShortcut()
        : QStringLiteral("Alt+Space");
    applyShortcutConfiguration(enabled, shortcut, &errorMessage);
}

QuickSearchController::~QuickSearchController()
{
    unregisterShortcut();
    if (qApp) {
        qApp->removeNativeEventFilter(this);
    }
    if (m_trayIcon) {
        m_trayIcon->hide();
        m_trayIcon->setContextMenu(nullptr);
    }
    delete m_trayMenu;
    m_trayMenu = nullptr;
}

QString QuickSearchController::shortcutStatusText() const
{
    return m_shortcutStatusText;
}

QString QuickSearchController::shortcut() const
{
    return m_shortcut.isEmpty() ? QStringLiteral("Alt+Space") : m_shortcut;
}

bool QuickSearchController::startHidden() const
{
    return m_startHidden;
}

bool QuickSearchController::trayAvailable() const
{
    return m_trayIcon != nullptr;
}

QString QuickSearchController::normalizedShortcut(const QString &shortcut,
                                                  QString *errorMessage)
{
    auto sequence = QKeySequence::fromString(shortcut.trimmed(), QKeySequence::PortableText);
    if (sequence.isEmpty()) {
        sequence = QKeySequence::fromString(shortcut.trimmed(), QKeySequence::NativeText);
    }
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("快捷键无效，请录入一个组合键，例如 Alt+Space。");
        }
        return {};
    }

    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers()
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    if (isModifierKey(combination.key()) || modifiers == Qt::NoModifier) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("全局快捷键必须包含 Ctrl、Alt、Shift 或 Win 修饰键。");
        }
        return {};
    }

#ifdef Q_OS_WIN
    unsigned int nativeModifiers = 0;
    unsigned int nativeVirtualKey = 0;
    if (!nativeShortcut(combination, &nativeModifiers, &nativeVirtualKey)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前按键不能注册为 Windows 全局快捷键。");
        }
        return {};
    }
#endif

    return QKeySequence(combination).toString(QKeySequence::PortableText);
}

QString QuickSearchController::shortcutFromKeyEvent(int key, int modifiers)
{
    const auto qtKey = static_cast<Qt::Key>(key);
    if (isModifierKey(qtKey) || qtKey == Qt::Key_unknown || qtKey == Qt::Key_Escape) {
        return {};
    }
    const auto qtModifiers = Qt::KeyboardModifiers(modifiers)
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    return normalizedShortcut(QKeySequence(QKeyCombination(qtModifiers, qtKey))
                                  .toString(QKeySequence::PortableText));
}

QPoint QuickSearchController::clampWindowPosition(const QPoint &requestedPosition,
                                                  const QSize &windowSize,
                                                  const QList<QRect> &availableGeometries,
                                                  const QPoint &fallbackPoint)
{
    if (availableGeometries.isEmpty()) {
        return requestedPosition;
    }

    const QSize normalizedSize(qMax(1, windowSize.width()), qMax(1, windowSize.height()));
    const QRect requestedRect(requestedPosition, normalizedSize);
    QRect targetGeometry;
    qint64 largestIntersectionArea = 0;
    for (const auto &geometry : availableGeometries) {
        const auto intersection = requestedRect.intersected(geometry);
        const auto area = static_cast<qint64>(intersection.width()) * intersection.height();
        if (area > largestIntersectionArea) {
            largestIntersectionArea = area;
            targetGeometry = geometry;
        }
    }

    if (!targetGeometry.isValid()) {
        for (const auto &geometry : availableGeometries) {
            if (geometry.contains(fallbackPoint)) {
                targetGeometry = geometry;
                break;
            }
        }
    }
    if (!targetGeometry.isValid()) {
        targetGeometry = availableGeometries.first();
    }

    const int x = normalizedSize.width() >= targetGeometry.width()
        ? targetGeometry.left()
        : qBound(targetGeometry.left(),
                 requestedPosition.x(),
                 targetGeometry.right() - normalizedSize.width() + 1);
    const int y = normalizedSize.height() >= targetGeometry.height()
        ? targetGeometry.top()
        : qBound(targetGeometry.top(),
                 requestedPosition.y(),
                 targetGeometry.bottom() - normalizedSize.height() + 1);
    return QPoint(x, y);
}

void QuickSearchController::requestQuickSearch()
{
    emit quickSearchRequested();
}

bool QuickSearchController::restoreMainWindow(QObject *windowObject)
{
    auto *window = qobject_cast<QWindow *>(windowObject);
    if (!window) {
        return false;
    }

    const auto requestId = ++m_mainWindowRestoreRequestId;
    restoreMainWindowOnce(window);
    scheduleMainWindowRestore(window, requestId, 0);
    return true;
}

bool QuickSearchController::isMainWindowForeground(QObject *windowObject) const
{
    auto *window = qobject_cast<QWindow *>(windowObject);
    if (!window) {
        return false;
    }

#ifdef Q_OS_WIN
    const auto windowHandle = reinterpret_cast<HWND>(window->winId());
    return isNativeWindowForeground(windowHandle);
#else
    return window->isVisible() && window->isActive();
#endif
}

bool QuickSearchController::restoreMainWindowOnce(QWindow *window) const
{
    if (!window) {
        return false;
    }

    window->showNormal();
    window->raise();
#ifdef Q_OS_WIN
    const auto windowHandle = reinterpret_cast<HWND>(window->winId());
    const auto foreground = restoreNativeWindowToForeground(windowHandle);
#else
    const auto foreground = window->isVisible() && window->isActive();
#endif
    window->requestActivate();
    return foreground || isMainWindowForeground(window);
}

void QuickSearchController::scheduleMainWindowRestore(QWindow *window,
                                                       quint64 requestId,
                                                       int attemptIndex)
{
    if (!window
        || requestId != m_mainWindowRestoreRequestId
        || attemptIndex < 0
        || attemptIndex >= static_cast<int>(kMainWindowRestoreRetryDelaysMs.size())) {
        return;
    }

    const QPointer<QWindow> guardedWindow(window);
    QTimer::singleShot(kMainWindowRestoreRetryDelaysMs[attemptIndex],
                       this,
                       [this, guardedWindow, requestId, attemptIndex]() {
        if (!guardedWindow || requestId != m_mainWindowRestoreRequestId) {
            return;
        }
        if (isMainWindowForeground(guardedWindow)) {
            emit mainWindowRestoreFinished(true);
            return;
        }

        const bool foreground = restoreMainWindowOnce(guardedWindow);
        if (foreground) {
            emit mainWindowRestoreFinished(true);
            return;
        }

        const int nextAttemptIndex = attemptIndex + 1;
        if (nextAttemptIndex < static_cast<int>(kMainWindowRestoreRetryDelaysMs.size())) {
            scheduleMainWindowRestore(guardedWindow, requestId, nextAttemptIndex);
        } else {
            emit mainWindowRestoreFinished(false);
        }
    });
}

QPoint QuickSearchController::restoredWindowPosition(int windowWidth, int windowHeight) const
{
    QList<QRect> geometries;
    const auto screens = QGuiApplication::screens();
    geometries.reserve(screens.size());
    for (const auto *screen : screens) {
        if (screen) {
            geometries.append(screen->availableGeometry());
        }
    }

    const auto cursorPosition = QCursor::pos();
    QPoint requestedPosition;
    if (m_settings && m_settings->hasQuickSearchWindowPosition()) {
        requestedPosition = m_settings->quickSearchWindowPosition();
    } else {
        QRect defaultGeometry;
        for (const auto &geometry : geometries) {
            if (geometry.contains(cursorPosition)) {
                defaultGeometry = geometry;
                break;
            }
        }
        if (!defaultGeometry.isValid() && !geometries.isEmpty()) {
            defaultGeometry = geometries.first();
        }
        requestedPosition = QPoint(
            defaultGeometry.left() + qMax(0, (defaultGeometry.width() - windowWidth) / 2),
            defaultGeometry.top() + qMax(0, qRound(defaultGeometry.height() * 0.12)));
    }
    return clampWindowPosition(requestedPosition,
                               QSize(windowWidth, windowHeight),
                               geometries,
                               cursorPosition);
}

QPoint QuickSearchController::rememberWindowPosition(int x,
                                                     int y,
                                                     int windowWidth,
                                                     int windowHeight)
{
    QList<QRect> geometries;
    const auto screens = QGuiApplication::screens();
    geometries.reserve(screens.size());
    for (const auto *screen : screens) {
        if (screen) {
            geometries.append(screen->availableGeometry());
        }
    }
    const auto position = clampWindowPosition(QPoint(x, y),
                                              QSize(windowWidth, windowHeight),
                                              geometries,
                                              QCursor::pos());
    if (m_settings) {
        m_settings->setQuickSearchWindowPosition(position);
        m_settings->sync();
    }
    return position;
}

bool QuickSearchController::applyShortcutConfiguration(bool enabled,
                                                       const QString &shortcut,
                                                       QString *errorMessage)
{
    QString normalizationError;
    const auto normalized = normalizedShortcut(shortcut, &normalizationError);
    if (normalized.isEmpty()) {
        if (errorMessage) *errorMessage = normalizationError;
        return false;
    }

    if (!enabled) {
        unregisterShortcut();
        m_enabled = false;
        m_shortcut = normalized;
        setShortcutStatusText(QStringLiteral("快捷搜索全局快捷键已关闭"));
        updateTrayToolTip();
        return true;
    }

    if (m_registered && m_enabled && m_shortcut == normalized) {
        setShortcutStatusText(QStringLiteral("全局快捷键已启用：%1").arg(normalized));
        return true;
    }

    const auto previousShortcut = m_shortcut;
    const auto previousEnabled = m_enabled;
    const auto previousRegistered = m_registered;
    unregisterShortcut();

    QString registrationError;
    if (!registerShortcut(normalized, &registrationError)) {
        if (previousRegistered && previousEnabled) {
            QString restoreError;
            registerShortcut(previousShortcut, &restoreError);
            m_enabled = true;
        }
        setShortcutStatusText(registrationError);
        if (errorMessage) *errorMessage = registrationError;
        return false;
    }

    m_enabled = true;
    m_shortcut = normalized;
    setShortcutStatusText(QStringLiteral("全局快捷键已启用：%1").arg(normalized));
    updateTrayToolTip();
    return true;
}

bool QuickSearchController::registerShortcut(const QString &shortcut, QString *errorMessage)
{
#ifdef Q_OS_WIN
    const auto sequence = QKeySequence::fromString(shortcut, QKeySequence::PortableText);
    unsigned int nativeModifiers = 0;
    unsigned int nativeVirtualKey = 0;
    if (sequence.count() != 1
        || !nativeShortcut(sequence[0], &nativeModifiers, &nativeVirtualKey)) {
        if (errorMessage) *errorMessage = QStringLiteral("当前按键不能注册为 Windows 全局快捷键。");
        return false;
    }
    if (!RegisterHotKey(nullptr,
                        kQuickSearchHotKeyId,
                        nativeModifiers,
                        nativeVirtualKey)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("快捷键 %1 注册失败，可能已被其他程序占用。").arg(shortcut);
        }
        return false;
    }
    m_nativeModifiers = nativeModifiers;
    m_nativeVirtualKey = nativeVirtualKey;
    m_registered = true;
    m_shortcut = shortcut;
    return true;
#else
    Q_UNUSED(shortcut);
    if (errorMessage) *errorMessage = QStringLiteral("当前系统暂不支持全局快捷键。");
    return false;
#endif
}

void QuickSearchController::unregisterShortcut()
{
#ifdef Q_OS_WIN
    if (m_registered) {
        UnregisterHotKey(nullptr, kQuickSearchHotKeyId);
    }
#endif
    m_registered = false;
    m_nativeModifiers = 0;
    m_nativeVirtualKey = 0;
}

bool QuickSearchController::setStartAtLogin(bool enabled, QString *errorMessage)
{
#ifdef Q_OS_WIN
    QSettings runSettings(QLatin1String(kRunRegistryPath), QSettings::NativeFormat);
    if (enabled) {
        const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        runSettings.setValue(QLatin1String(kRunRegistryValue),
                             QStringLiteral("\"%1\" --background").arg(executable));
    } else {
        runSettings.remove(QLatin1String(kRunRegistryValue));
    }
    runSettings.sync();
    if (runSettings.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = enabled
                ? QStringLiteral("无法写入 Windows 开机启动设置。")
                : QStringLiteral("无法移除 Windows 开机启动设置。");
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(enabled);
    if (errorMessage) *errorMessage = QStringLiteral("当前系统暂不支持开机启动设置。");
    return false;
#endif
}

bool QuickSearchController::nativeEventFilter(const QByteArray &eventType,
                                              void *message,
                                              qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage
        && nativeMessage->message == WM_HOTKEY
        && nativeMessage->wParam == kQuickSearchHotKeyId) {
        emit quickSearchRequested();
        return true;
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}

void QuickSearchController::createTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayMenu = new QMenu;
    m_quickSearchAction = m_trayMenu->addAction(QStringLiteral("快捷搜索"));
    m_showMainWindowAction = m_trayMenu->addAction(QStringLiteral("显示主窗口"));
    m_trayMenu->addSeparator();
    m_quitAction = m_trayMenu->addAction(QStringLiteral("退出影资管家"));

    connect(m_quickSearchAction, &QAction::triggered, this, &QuickSearchController::quickSearchRequested);
    connect(m_showMainWindowAction, &QAction::triggered, this, &QuickSearchController::showMainWindowRequested);
    connect(m_quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/app.ico")), this);
    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            emit quickSearchRequested();
        } else if (reason == QSystemTrayIcon::DoubleClick) {
            emit showMainWindowRequested();
        }
    });
    updateTrayToolTip();
    m_trayIcon->show();
}

void QuickSearchController::setShortcutStatusText(const QString &statusText)
{
    if (m_shortcutStatusText == statusText) {
        return;
    }
    m_shortcutStatusText = statusText;
    emit shortcutStatusChanged();
}

void QuickSearchController::updateTrayToolTip()
{
    if (!m_trayIcon) {
        return;
    }
    const auto suffix = m_enabled && !m_shortcut.isEmpty()
        ? QStringLiteral(" · %1").arg(m_shortcut)
        : QString();
    m_trayIcon->setToolTip(QStringLiteral("影资管家%1").arg(suffix));
}
