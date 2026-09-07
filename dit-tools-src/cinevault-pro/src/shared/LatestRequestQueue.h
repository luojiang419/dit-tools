#pragma once

#include <functional>
#include <utility>

// Owner-thread only. One active operation and one replaceable pending request.
// The operation must call complete() on the owner thread when its result arrives.
class LatestRequestQueue {
public:
    void submit(std::function<void()> request)
    {
        if (m_running) {
            m_pending = std::move(request);
            return;
        }
        m_running = true;
        request();
    }

    void complete()
    {
        m_running = false;
        if (auto next = std::exchange(m_pending, {})) {
            submit(std::move(next));
        }
    }

private:
    bool m_running = false;
    std::function<void()> m_pending;
};
