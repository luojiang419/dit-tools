#include "application/IndexingWorkCoordinator.h"

#include <QMutexLocker>

#include <utility>

IndexingWorkCoordinator::Lease::Lease(IndexingWorkCoordinator *owner, Resource resource)
    : m_owner(owner)
    , m_resource(resource)
{
}

IndexingWorkCoordinator::Lease::~Lease()
{
    reset();
}

IndexingWorkCoordinator::Lease::Lease(Lease &&other) noexcept
    : m_owner(std::exchange(other.m_owner, nullptr))
    , m_resource(other.m_resource)
{
}

IndexingWorkCoordinator::Lease &IndexingWorkCoordinator::Lease::operator=(Lease &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    m_owner = std::exchange(other.m_owner, nullptr);
    m_resource = other.m_resource;
    return *this;
}

bool IndexingWorkCoordinator::Lease::isValid() const
{
    return m_owner != nullptr;
}

IndexingWorkCoordinator::Lease::operator bool() const
{
    return isValid();
}

void IndexingWorkCoordinator::Lease::reset()
{
    if (!m_owner) {
        return;
    }
    auto *owner = std::exchange(m_owner, nullptr);
    owner->release(m_resource);
}

IndexingWorkCoordinator::IndexingWorkCoordinator(qsizetype maxQueuedRequests, QObject *parent)
    : QObject(parent)
    , m_maxQueuedRequests(qMax<qsizetype>(1, maxQueuedRequests))
{
}

IndexingWorkCoordinator::Lease IndexingWorkCoordinator::acquire(const Request &request)
{
    QMutexLocker locker(&m_mutex);
    if (m_shutdown || request.generation != m_generation) {
        return {};
    }

    const auto canAcquireImmediately = !resourceInUseLocked(request.resource)
        && requestIsEligibleLocked(request)
        && bestEligibleWaiterLocked(request.resource) == nullptr;
    if (canAcquireImmediately) {
        setResourceInUseLocked(request.resource, true);
        return Lease(this, request.resource);
    }

    Waiter waiter{request, m_nextSequence++, false};
    if (m_waiters.size() >= m_maxQueuedRequests) {
        if (request.priority == Priority::Background) {
            return {};
        }
        auto victim = m_waiters.end();
        for (auto it = m_waiters.end(); it != m_waiters.begin();) {
            --it;
            if ((*it)->request.priority == Priority::Background) {
                victim = it;
                break;
            }
        }
        if (victim == m_waiters.end()) {
            return {};
        }
        (*victim)->cancelled = true;
        m_waiters.erase(victim);
        m_changed.wakeAll();
    }
    m_waiters.append(&waiter);

    while (true) {
        if (m_shutdown || waiter.cancelled || request.generation != m_generation) {
            m_waiters.removeOne(&waiter);
            m_changed.wakeAll();
            return {};
        }
        if (!resourceInUseLocked(request.resource)
            && requestIsEligibleLocked(request)
            && bestEligibleWaiterLocked(request.resource) == &waiter) {
            m_waiters.removeOne(&waiter);
            setResourceInUseLocked(request.resource, true);
            return Lease(this, request.resource);
        }
        m_changed.wait(&m_mutex);
    }
}

quint64 IndexingWorkCoordinator::currentGeneration() const
{
    QMutexLocker locker(&m_mutex);
    return m_generation;
}

qsizetype IndexingWorkCoordinator::queuedRequestCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_waiters.size();
}

bool IndexingWorkCoordinator::isSystemIdle() const
{
    QMutexLocker locker(&m_mutex);
    return m_systemIdle;
}

bool IndexingWorkCoordinator::isShutdown() const
{
    QMutexLocker locker(&m_mutex);
    return m_shutdown;
}

void IndexingWorkCoordinator::setSystemIdle(bool idle)
{
    QMutexLocker locker(&m_mutex);
    if (m_systemIdle == idle || m_shutdown) {
        return;
    }
    m_systemIdle = idle;
    m_changed.wakeAll();
}

void IndexingWorkCoordinator::advanceGeneration()
{
    QMutexLocker locker(&m_mutex);
    if (m_shutdown) {
        return;
    }
    ++m_generation;
    m_changed.wakeAll();
}

void IndexingWorkCoordinator::shutdown()
{
    QMutexLocker locker(&m_mutex);
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    ++m_generation;
    m_changed.wakeAll();
}

bool IndexingWorkCoordinator::resourceInUseLocked(Resource resource) const
{
    return resource == Resource::HeavyIo ? m_heavyIoInUse : m_sqliteWriterInUse;
}

void IndexingWorkCoordinator::setResourceInUseLocked(Resource resource, bool inUse)
{
    if (resource == Resource::HeavyIo) {
        m_heavyIoInUse = inUse;
    } else {
        m_sqliteWriterInUse = inUse;
    }
}

bool IndexingWorkCoordinator::requestIsEligibleLocked(const Request &request) const
{
    return request.generation == m_generation && (!request.requiresIdle || m_systemIdle);
}

IndexingWorkCoordinator::Waiter *IndexingWorkCoordinator::bestEligibleWaiterLocked(
    Resource resource) const
{
    Waiter *best = nullptr;
    for (auto *waiter : m_waiters) {
        if (!waiter || waiter->cancelled || waiter->request.resource != resource
            || !requestIsEligibleLocked(waiter->request)) {
            continue;
        }
        if (!best
            || (waiter->request.priority == Priority::Foreground
                && best->request.priority == Priority::Background)
            || (waiter->request.priority == best->request.priority
                && waiter->sequence < best->sequence)) {
            best = waiter;
        }
    }
    return best;
}

void IndexingWorkCoordinator::release(Resource resource)
{
    QMutexLocker locker(&m_mutex);
    setResourceInUseLocked(resource, false);
    m_changed.wakeAll();
}
