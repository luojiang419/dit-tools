#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class ScopedBackgroundThreadPriority final {
public:
    ScopedBackgroundThreadPriority()
    {
#ifdef Q_OS_WIN
        m_previousPriority = GetThreadPriority(GetCurrentThread());
        m_shouldRestore = m_previousPriority != THREAD_PRIORITY_ERROR_RETURN
            && SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != FALSE;
#endif
    }

    ~ScopedBackgroundThreadPriority()
    {
#ifdef Q_OS_WIN
        if (m_shouldRestore) {
            SetThreadPriority(GetCurrentThread(), m_previousPriority);
        }
#endif
    }

    ScopedBackgroundThreadPriority(const ScopedBackgroundThreadPriority &) = delete;
    ScopedBackgroundThreadPriority &operator=(const ScopedBackgroundThreadPriority &) = delete;

private:
#ifdef Q_OS_WIN
    int m_previousPriority = THREAD_PRIORITY_NORMAL;
    bool m_shouldRestore = false;
#endif
};
