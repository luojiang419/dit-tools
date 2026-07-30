#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

class IndexingWorkCoordinator final : public QObject {
public:
    enum class Resource {
        HeavyIo,
        SqliteWriter
    };

    enum class Priority {
        Foreground,
        Background
    };

    struct Request {
        Resource resource = Resource::HeavyIo;
        Priority priority = Priority::Background;
        bool requiresIdle = false;
        quint64 generation = 0;
        int timeoutMs = -1;
    };

    class Lease final {
    public:
        Lease() = default;
        ~Lease();

        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;

        [[nodiscard]] bool isValid() const;
        explicit operator bool() const;
        void reset();

    private:
        friend class IndexingWorkCoordinator;
        Lease(IndexingWorkCoordinator *owner, Resource resource);

        IndexingWorkCoordinator *m_owner = nullptr;
        Resource m_resource = Resource::HeavyIo;
    };

    explicit IndexingWorkCoordinator(qsizetype maxQueuedRequests = 64,
                                     QObject *parent = nullptr);

    [[nodiscard]] Lease acquire(const Request &request);
    [[nodiscard]] quint64 currentGeneration() const;
    [[nodiscard]] qsizetype queuedRequestCount() const;
    [[nodiscard]] bool isSystemIdle() const;
    [[nodiscard]] bool isShutdown() const;

    void setSystemIdle(bool idle);
    void advanceGeneration();
    void shutdown();

private:
    struct Waiter {
        Request request;
        quint64 sequence = 0;
        bool cancelled = false;
    };

    [[nodiscard]] bool resourceInUseLocked(Resource resource) const;
    void setResourceInUseLocked(Resource resource, bool inUse);
    [[nodiscard]] bool requestIsEligibleLocked(const Request &request) const;
    [[nodiscard]] Waiter *bestEligibleWaiterLocked(Resource resource) const;
    void release(Resource resource);

    mutable QMutex m_mutex;
    QWaitCondition m_changed;
    QList<Waiter *> m_waiters;
    qsizetype m_maxQueuedRequests = 64;
    quint64 m_generation = 1;
    quint64 m_nextSequence = 1;
    bool m_systemIdle = false;
    bool m_shutdown = false;
    bool m_heavyIoInUse = false;
    bool m_sqliteWriterInUse = false;
};
