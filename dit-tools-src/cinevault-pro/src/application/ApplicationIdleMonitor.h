#pragma once

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>

class ApplicationIdleMonitor final : public QObject {
    Q_OBJECT

public:
    explicit ApplicationIdleMonitor(QObject *parent = nullptr);
    ~ApplicationIdleMonitor() override;

    void start(int idleTimeoutMs);
    void stop();
    void recordActivity();

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isIdle() const;
    [[nodiscard]] int idleTimeoutMs() const;

signals:
    void becameIdle();
    void activityResumed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static bool isUserActivityEvent(QEvent::Type type);

    QPointer<QObject> m_eventSource;
    QTimer m_idleTimer;
    int m_idleTimeoutMs = 0;
    bool m_active = false;
    bool m_idle = false;
};
