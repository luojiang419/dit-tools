#include "application/IndexingWorkCoordinator.h"

#include <QtConcurrent>
#include <QtTest>

#include <QElapsedTimer>

#include <atomic>

namespace {
IndexingWorkCoordinator::Request request(
    IndexingWorkCoordinator &coordinator,
    IndexingWorkCoordinator::Resource resource,
    IndexingWorkCoordinator::Priority priority = IndexingWorkCoordinator::Priority::Foreground,
    bool requiresIdle = false)
{
    return {resource, priority, requiresIdle, coordinator.currentGeneration()};
}
}

class IndexingWorkCoordinatorTest final : public QObject {
    Q_OBJECT

private slots:
    void heavyIoAllowsOnlyOneLease()
    {
        IndexingWorkCoordinator coordinator;
        auto first = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(first);

        std::atomic_bool acquired = false;
        auto future = QtConcurrent::run([&]() {
            auto second = coordinator.acquire(request(
                coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
            acquired = second.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        QVERIFY(!acquired.load());
        first.reset();
        QTRY_VERIFY(future.isFinished());
        QVERIFY(acquired.load());
    }

    void sqliteWriterAllowsOnlyOneLeaseAndIsIndependentFromHeavyIo()
    {
        IndexingWorkCoordinator coordinator;
        auto writer = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::SqliteWriter));
        auto heavyIo = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(writer);
        QVERIFY(heavyIo);

        std::atomic_bool secondWriterAcquired = false;
        auto future = QtConcurrent::run([&]() {
            auto second = coordinator.acquire(request(
                coordinator, IndexingWorkCoordinator::Resource::SqliteWriter));
            secondWriterAcquired = second.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        heavyIo.reset();
        QTest::qWait(20);
        QVERIFY(!secondWriterAcquired.load());
        writer.reset();
        QTRY_VERIFY(future.isFinished());
        QVERIFY(secondWriterAcquired.load());
    }

    void foregroundWaiterRunsBeforeBackgroundWaiter()
    {
        IndexingWorkCoordinator coordinator;
        coordinator.setSystemIdle(true);
        auto owner = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(owner);

        std::atomic_int order = 0;
        std::atomic_int backgroundOrder = 0;
        std::atomic_int foregroundOrder = 0;
        auto background = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator,
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Background,
                true));
            if (lease) {
                backgroundOrder = ++order;
            }
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        auto foreground = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
            if (lease) {
                foregroundOrder = ++order;
            }
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 2);
        owner.reset();
        QTRY_VERIFY(background.isFinished() && foreground.isFinished());
        QCOMPARE(foregroundOrder.load(), 1);
        QCOMPARE(backgroundOrder.load(), 2);
    }

    void idleRequiredWorkWaitsUntilSystemIsIdle()
    {
        IndexingWorkCoordinator coordinator;
        std::atomic_bool acquired = false;
        auto future = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator,
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Background,
                true));
            acquired = lease.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        QTest::qWait(20);
        QVERIFY(!acquired.load());
        coordinator.setSystemIdle(true);
        QTRY_VERIFY(future.isFinished());
        QVERIFY(acquired.load());
    }

    void fullQueueRejectsNewBackgroundRequestQuickly()
    {
        IndexingWorkCoordinator coordinator(1);
        auto owner = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(owner);
        auto waiting = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator,
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Background));
            return lease.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);

        QElapsedTimer timer;
        timer.start();
        auto rejected = coordinator.acquire(request(
            coordinator,
            IndexingWorkCoordinator::Resource::HeavyIo,
            IndexingWorkCoordinator::Priority::Background));
        QVERIFY(!rejected);
        QVERIFY(timer.elapsed() < 50);
        coordinator.shutdown();
        QTRY_VERIFY(waiting.isFinished());
    }

    void foregroundRequestDisplacesQueuedBackgroundWhenFull()
    {
        IndexingWorkCoordinator coordinator(1);
        auto owner = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(owner);
        auto background = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator,
                IndexingWorkCoordinator::Resource::HeavyIo,
                IndexingWorkCoordinator::Priority::Background));
            return lease.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        auto foreground = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
            return lease.isValid();
        });
        QTRY_VERIFY(background.isFinished());
        QVERIFY(!background.result());
        QCOMPARE(coordinator.queuedRequestCount(), 1);
        owner.reset();
        QTRY_VERIFY(foreground.isFinished());
        QVERIFY(foreground.result());
    }

    void generationChangeCancelsOldWaiters()
    {
        IndexingWorkCoordinator coordinator;
        auto owner = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(owner);
        auto waiting = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
            return lease.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        coordinator.advanceGeneration();
        QTRY_VERIFY(waiting.isFinished());
        QVERIFY(!waiting.result());
        QCOMPARE(coordinator.queuedRequestCount(), 0);
    }

    void shutdownWakesAndCancelsAllWaiters()
    {
        IndexingWorkCoordinator coordinator;
        auto owner = coordinator.acquire(request(
            coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
        QVERIFY(owner);
        auto waiting = QtConcurrent::run([&]() {
            auto lease = coordinator.acquire(request(
                coordinator, IndexingWorkCoordinator::Resource::HeavyIo));
            return lease.isValid();
        });
        QTRY_COMPARE(coordinator.queuedRequestCount(), 1);
        coordinator.shutdown();
        QTRY_VERIFY(waiting.isFinished());
        QVERIFY(!waiting.result());
        QVERIFY(coordinator.isShutdown());
        QCOMPARE(coordinator.queuedRequestCount(), 0);
    }
};

QTEST_GUILESS_MAIN(IndexingWorkCoordinatorTest)

#include "IndexingWorkCoordinatorTest.moc"
