#pragma once

#include "application/ApplicationIdleMonitor.h"

#include <QObject>
#include <QTimer>

class AppSettings;
class LocalSearchAssistantRuntime;

class SearchAssistantLifecycleController final : public QObject {
    Q_OBJECT

public:
    explicit SearchAssistantLifecycleController(
        AppSettings *settings,
        LocalSearchAssistantRuntime *runtime,
        QObject *parent = nullptr);

    void start();
    void applySettings();
    void recordUserActivity();
    void recordSearchIntent();

    [[nodiscard]] bool isStarted() const;
    [[nodiscard]] bool isIdleUnloaded() const;
    [[nodiscard]] int autoUnloadMinutes() const;

signals:
    void lifecycleChanged();

private:
    void schedulePreload(int delayMs);
    void preload();
    void unloadForIdle();
    void resumeFromIdle();

    AppSettings *m_settings = nullptr;
    LocalSearchAssistantRuntime *m_runtime = nullptr;
    ApplicationIdleMonitor m_idleMonitor;
    QTimer m_preloadTimer;
    bool m_started = false;
    bool m_idleUnloaded = false;
    bool m_searchIntentActive = false;
};
