#include "core/jobs/JobEngine.h"

#include <QtTest>

namespace {
bool containsJob(const QVector<Job> &jobs, qint64 id)
{
    for (const auto &job : jobs) {
        if (job.id == id) {
            return true;
        }
    }
    return false;
}
}

class JobEngineTest : public QObject {
    Q_OBJECT

private slots:
    void clearCompletedJobsKeepsActiveAndFailedJobs();
};

void JobEngineTest::clearCompletedJobsKeepsActiveAndFailedJobs()
{
    JobEngine engine(nullptr);
    QSignalSpy spy(&engine, &JobEngine::jobsChanged);

    const auto completedId = engine.createJob(JobType::Scan, QStringLiteral("已完成"), QStringLiteral("处理中"));
    const auto runningId = engine.createJob(JobType::Thumbnail, QStringLiteral("进行中"), QStringLiteral("处理中"));
    const auto pendingId = engine.queueJob(JobType::ContentAnalysis, QStringLiteral("排队中"), QStringLiteral("等待"));
    const auto failedId = engine.createJob(JobType::Metadata, QStringLiteral("失败"), QStringLiteral("处理中"));

    engine.completeJob(completedId, QStringLiteral("完成"));
    engine.failJob(failedId, QStringLiteral("失败"));
    engine.clearCompletedJobs();

    const auto jobs = engine.jobs();
    QCOMPARE(jobs.size(), 3);
    QVERIFY(!containsJob(jobs, completedId));
    QVERIFY(containsJob(jobs, runningId));
    QVERIFY(containsJob(jobs, pendingId));
    QVERIFY(containsJob(jobs, failedId));
    QVERIFY(spy.count() >= 7);
}

QTEST_APPLESS_MAIN(JobEngineTest)

#include "JobEngineTest.moc"
