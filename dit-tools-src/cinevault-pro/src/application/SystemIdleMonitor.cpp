#include "application/SystemIdleMonitor.h"

#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

SystemIdleMonitor::SystemIdleMonitor(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, &SystemIdleMonitor::pollNow);
}

void SystemIdleMonitor::start(qint64 idleThresholdMs, int pollIntervalMs)
{
    m_idleThresholdMs = qMax<qint64>(1, idleThresholdMs);
    m_pollTimer.setInterval(qMax(250, pollIntervalMs));
    m_active = true;
    pollNow();
    m_pollTimer.start();
}

void SystemIdleMonitor::stop()
{
    m_pollTimer.stop();
    m_active = false;
    m_idle = false;
    m_idleDurationMs = 0;
}

bool SystemIdleMonitor::isActive() const
{
    return m_active;
}

bool SystemIdleMonitor::isIdle() const
{
    return m_idle;
}

qint64 SystemIdleMonitor::idleDurationMs() const
{
    return m_idleDurationMs;
}

qint64 SystemIdleMonitor::idleThresholdMs() const
{
    return m_idleThresholdMs;
}

void SystemIdleMonitor::setIdleDurationProviderForTesting(std::function<qint64()> provider)
{
    m_testProvider = std::move(provider);
}

void SystemIdleMonitor::pollNow()
{
    if (!m_active) {
        return;
    }
    const auto duration = querySystemIdleDurationMs();
    if (duration < 0) {
        return;
    }
    if (m_idleDurationMs != duration) {
        m_idleDurationMs = duration;
        emit idleDurationChanged(duration);
    }

    const auto nowIdle = duration >= m_idleThresholdMs;
    if (nowIdle == m_idle) {
        return;
    }
    m_idle = nowIdle;
    if (m_idle) {
        emit becameIdle();
    } else {
        emit activityResumed();
    }
}

qint64 SystemIdleMonitor::querySystemIdleDurationMs() const
{
    if (m_testProvider) {
        return m_testProvider();
    }
#ifdef Q_OS_WIN
    LASTINPUTINFO inputInfo{};
    inputInfo.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&inputInfo)) {
        return -1;
    }
    const DWORD currentTick = GetTickCount();
    return static_cast<qint64>(static_cast<DWORD>(currentTick - inputInfo.dwTime));
#else
    return -1;
#endif
}
