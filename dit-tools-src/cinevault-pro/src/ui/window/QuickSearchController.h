#pragma once

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

class QAction;
class AppSettings;
class QMenu;
class QSystemTrayIcon;
class QWindow;

class QuickSearchController final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY shortcutStatusChanged)
    Q_PROPERTY(QString shortcutStatusText READ shortcutStatusText NOTIFY shortcutStatusChanged)
    Q_PROPERTY(bool startHidden READ startHidden CONSTANT)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)

public:
    explicit QuickSearchController(AppSettings *settings, QObject *parent = nullptr);
    ~QuickSearchController() override;

    QString shortcut() const;
    QString shortcutStatusText() const;
    bool startHidden() const;
    bool trayAvailable() const;

    bool applyShortcutConfiguration(bool enabled,
                                    const QString &shortcut,
                                    QString *errorMessage = nullptr);
    bool setStartAtLogin(bool enabled, QString *errorMessage = nullptr);

    static QString normalizedShortcut(const QString &shortcut,
                                      QString *errorMessage = nullptr);
    static QString shortcutFromKeyEvent(int key, int modifiers);
    static QPoint clampWindowPosition(const QPoint &requestedPosition,
                                      const QSize &windowSize,
                                      const QList<QRect> &availableGeometries,
                                      const QPoint &fallbackPoint);

    Q_INVOKABLE void requestQuickSearch();
    Q_INVOKABLE bool restoreMainWindow(QObject *windowObject);
    Q_INVOKABLE bool isMainWindowForeground(QObject *windowObject) const;
    Q_INVOKABLE QPoint restoredWindowPosition(int windowWidth, int windowHeight) const;
    Q_INVOKABLE QPoint rememberWindowPosition(int x, int y, int windowWidth, int windowHeight);

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

signals:
    void quickSearchRequested();
    void showMainWindowRequested();
    void mainWindowRestoreFinished(bool foreground);
    void shortcutStatusChanged();

private:
    void createTrayIcon();
    void unregisterShortcut();
    bool registerShortcut(const QString &shortcut, QString *errorMessage);
    void setShortcutStatusText(const QString &statusText);
    void updateTrayToolTip();
    bool restoreMainWindowOnce(QWindow *window) const;
    void scheduleMainWindowRestore(QWindow *window, quint64 requestId, int attemptIndex);

    AppSettings *m_settings = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_quickSearchAction = nullptr;
    QAction *m_showMainWindowAction = nullptr;
    QAction *m_quitAction = nullptr;
    QString m_shortcut;
    QString m_shortcutStatusText;
    unsigned int m_nativeModifiers = 0;
    unsigned int m_nativeVirtualKey = 0;
    bool m_registered = false;
    bool m_enabled = false;
    bool m_startHidden = false;
    quint64 m_mainWindowRestoreRequestId = 0;
};
