#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

class SystemIdleMonitor final : public QObject {
    Q_OBJECT

public:
    explicit SystemIdleMonitor(QObject *parent = nullptr);

    void start(qint64 idleThresholdMs = 60 * 60 * 1000, int pollIntervalMs = 5000);
    void stop();

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isIdle() const;
    [[nodiscard]] qint64 idleDurationMs() const;
    [[nodiscard]] qint64 idleThresholdMs() const;

    void setIdleDurationProviderForTesting(std::function<qint64()> provider);

public slots:
    void pollNow();

signals:
    void becameIdle();
    void activityResumed();
    void idleDurationChanged(qint64 idleDurationMs);

private:
    qint64 querySystemIdleDurationMs() const;

    QTimer m_pollTimer;
    std::function<qint64()> m_testProvider;
    qint64 m_idleDurationMs = 0;
    qint64 m_idleThresholdMs = 60 * 60 * 1000;
    bool m_active = false;
    bool m_idle = false;
};
