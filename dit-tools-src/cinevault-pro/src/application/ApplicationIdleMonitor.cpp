#include "application/ApplicationIdleMonitor.h"

#include <QCoreApplication>
#include <QEvent>

ApplicationIdleMonitor::ApplicationIdleMonitor(QObject *parent)
    : QObject(parent)
    , m_eventSource(QCoreApplication::instance())
{
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_idleTimer, &QTimer::timeout, this, [this]() {
        if (!m_active || m_idle) {
            return;
        }
        m_idle = true;
        emit becameIdle();
    });
}

ApplicationIdleMonitor::~ApplicationIdleMonitor()
{
    stop();
}

void ApplicationIdleMonitor::start(int idleTimeoutMs)
{
    m_idleTimeoutMs = qMax(1, idleTimeoutMs);
    m_idle = false;
    if (!m_active) {
        m_active = true;
        if (m_eventSource) {
            m_eventSource->installEventFilter(this);
        }
    }
    m_idleTimer.start(m_idleTimeoutMs);
}

void ApplicationIdleMonitor::stop()
{
    m_idleTimer.stop();
    if (m_active && m_eventSource) {
        m_eventSource->removeEventFilter(this);
    }
    m_active = false;
    m_idle = false;
}

void ApplicationIdleMonitor::recordActivity()
{
    if (!m_active) {
        return;
    }
    const auto wasIdle = m_idle;
    m_idle = false;
    m_idleTimer.start(m_idleTimeoutMs);
    if (wasIdle) {
        emit activityResumed();
    }
}

bool ApplicationIdleMonitor::isActive() const
{
    return m_active;
}

bool ApplicationIdleMonitor::isIdle() const
{
    return m_idle;
}

int ApplicationIdleMonitor::idleTimeoutMs() const
{
    return m_idleTimeoutMs;
}

bool ApplicationIdleMonitor::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (m_active && event && isUserActivityEvent(event->type())) {
        recordActivity();
    }
    return false;
}

bool ApplicationIdleMonitor::isUserActivityEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Shortcut:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::NativeGesture:
        return true;
    default:
        return false;
    }
}
