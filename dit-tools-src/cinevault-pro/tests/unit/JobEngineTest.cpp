#include "core/jobs/JobEngine.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
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
    void clearFinishedJobsKeepsOnlyActiveJobs();
    void removeFinishedJobPersistsAndRejectsActiveJob();
    void retrySchedulingClearsOnlyReplacedFailedJobs();
    void reloadJobsRestoresHistoryAndAdvancesIds();
    void projectScopedUpdatesDoNotMutateSameIdInCurrentProject();
    void reloadJobsCapsLoadedHistory();
    void persistenceFailureDoesNotPublishInMemoryJob();
    void progressPersistenceDoesNotBlockUiOnWriterLock();
};

void JobEngineTest::clearFinishedJobsKeepsOnlyActiveJobs()
{
    JobEngine engine(nullptr);
    QSignalSpy spy(&engine, &JobEngine::jobsChanged);

    const auto completedId = engine.createJob(JobType::Scan, QStringLiteral("已完成"), QStringLiteral("处理中"));
    const auto runningId = engine.createJob(JobType::Thumbnail, QStringLiteral("进行中"), QStringLiteral("处理中"));
    const auto pendingId = engine.queueJob(JobType::ContentAnalysis, QStringLiteral("排队中"), QStringLiteral("等待"));
    const auto failedId = engine.createJob(JobType::Metadata, QStringLiteral("失败"), QStringLiteral("处理中"));

    engine.completeJob(completedId, QStringLiteral("完成"));
    engine.failJob(failedId, QStringLiteral("失败"));
    engine.clearFinishedJobs();

    const auto jobs = engine.jobs();
    QCOMPARE(jobs.size(), 2);
    QVERIFY(!containsJob(jobs, completedId));
    QVERIFY(containsJob(jobs, runningId));
    QVERIFY(containsJob(jobs, pendingId));
    QVERIFY(!containsJob(jobs, failedId));
    QVERIFY(spy.count() >= 7);
}

void JobEngineTest::removeFinishedJobPersistsAndRejectsActiveJob()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    DatabaseManager databaseManager;
    QString errorMessage;
    QVERIFY2(databaseManager.openProjectDatabase(
                 QDir(temp.path()).filePath(QStringLiteral("remove-finished.cvdb")),
                 &errorMessage),
             qPrintable(errorMessage));

    JobEngine engine(&databaseManager);
    const auto finishedId = engine.createJob(
        JobType::Scan, QStringLiteral("已完成"), QStringLiteral("处理中"));
    const auto runningId = engine.createJob(
        JobType::Thumbnail, QStringLiteral("进行中"), QStringLiteral("处理中"));
    engine.completeJob(finishedId, QStringLiteral("完成"));
    engine.waitForPersistence();

    QVERIFY(engine.removeFinishedJob(finishedId));
    QVERIFY(!engine.removeFinishedJob(runningId));
    QVERIFY(!containsJob(engine.jobs(), finishedId));
    QVERIFY(containsJob(engine.jobs(), runningId));

    QSqlQuery query(databaseManager.database());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM job WHERE id = ?"));
    query.addBindValue(finishedId);
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
}

void JobEngineTest::retrySchedulingClearsOnlyReplacedFailedJobs()
{
    JobEngine engine(nullptr);
    const auto replaced = engine.createJob(
        JobType::Thumbnail, QStringLiteral("旧缩略图"), QStringLiteral("处理中"), 7);
    const auto unrelatedType = engine.createJob(
        JobType::Metadata, QStringLiteral("旧元数据"), QStringLiteral("处理中"), 7);
    const auto unrelatedSource = engine.createJob(
        JobType::Thumbnail, QStringLiteral("其他素材源"), QStringLiteral("处理中"), 8);
    const auto active = engine.createJob(
        JobType::Thumbnail, QStringLiteral("新缩略图"), QStringLiteral("处理中"), 7);
    engine.failJob(replaced, QStringLiteral("可恢复失败"));
    engine.failJob(unrelatedType, QStringLiteral("其他类型失败"));
    engine.failJob(unrelatedSource, QStringLiteral("其他素材源失败"));

    engine.clearFailedJobsForRetry(7, {JobType::Thumbnail});

    const auto jobs = engine.jobs();
    QVERIFY(!containsJob(jobs, replaced));
    QVERIFY(containsJob(jobs, unrelatedType));
    QVERIFY(containsJob(jobs, unrelatedSource));
    QVERIFY(containsJob(jobs, active));
}

void JobEngineTest::reloadJobsRestoresHistoryAndAdvancesIds()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(temp.path()).filePath(QStringLiteral("jobs.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));

    qint64 runningId = 0;
    qint64 completedId = 0;
    JobSubject subject;
    subject.kind = QStringLiteral("frame");
    subject.key = QStringLiteral("video-42#120");
    subject.name = QStringLiteral("第 120 帧");
    subject.path = QStringLiteral("D:/media/clip.mp4");
    subject.thumbnailPath = QStringLiteral("D:/cache/frame-120.webp");
    subject.thumbnailStatus = ThumbnailStatus::Success;
    subject.typeLabel = QStringLiteral("视频帧");
    JobProgressContext progressContext;
    progressContext.currentStep = 2;
    progressContext.totalSteps = 4;
    progressContext.stepLabel = QStringLiteral("分析画面");
    progressContext.currentItem = 120;
    progressContext.totalItems = 240;
    progressContext.unitLabel = QStringLiteral("帧");
    progressContext.currentFrameNumber = 120;
    progressContext.extraLabel = QStringLiteral("场景识别");
    {
        JobEngine initialEngine(&databaseManager);
        runningId = initialEngine.createJob(JobType::Thumbnail,
                                             QStringLiteral("生成缩略图"),
                                             QStringLiteral("处理中"),
                                             0,
                                             subject,
                                             progressContext);
        completedId = initialEngine.createJob(JobType::Scan,
                                              QStringLiteral("建立索引"),
                                              QStringLiteral("处理中"),
                                              0);
        initialEngine.completeJob(completedId, QStringLiteral("完成"));
        initialEngine.waitForPersistence();
    }

    JobEngine restoredEngine(&databaseManager);
    restoredEngine.reloadJobs();
    const auto restoredJobs = restoredEngine.jobs();
    QCOMPARE(restoredJobs.size(), 2);

    const Job *restoredRunningJob = nullptr;
    for (const auto &job : restoredJobs) {
        if (job.id == runningId) {
            restoredRunningJob = &job;
            break;
        }
    }
    QVERIFY(restoredRunningJob);
    QCOMPARE(restoredRunningJob->state, JobState::Failed);
    QVERIFY(restoredRunningJob->errorMessage.contains(QStringLiteral("任务已中断")));
    QCOMPARE(restoredRunningJob->subject.kind, subject.kind);
    QCOMPARE(restoredRunningJob->subject.key, subject.key);
    QCOMPARE(restoredRunningJob->subject.name, subject.name);
    QCOMPARE(restoredRunningJob->subject.path, subject.path);
    QCOMPARE(restoredRunningJob->subject.thumbnailPath, subject.thumbnailPath);
    QCOMPARE(restoredRunningJob->subject.thumbnailStatus, subject.thumbnailStatus);
    QCOMPARE(restoredRunningJob->subject.typeLabel, subject.typeLabel);
    QCOMPARE(restoredRunningJob->progressContext.currentStep, progressContext.currentStep);
    QCOMPARE(restoredRunningJob->progressContext.totalSteps, progressContext.totalSteps);
    QCOMPARE(restoredRunningJob->progressContext.stepLabel, progressContext.stepLabel);
    QCOMPARE(restoredRunningJob->progressContext.currentItem, progressContext.currentItem);
    QCOMPARE(restoredRunningJob->progressContext.totalItems, progressContext.totalItems);
    QCOMPARE(restoredRunningJob->progressContext.unitLabel, progressContext.unitLabel);
    QCOMPARE(restoredRunningJob->progressContext.currentFrameNumber,
             progressContext.currentFrameNumber);
    QCOMPARE(restoredRunningJob->progressContext.extraLabel, progressContext.extraLabel);

    const auto nextId = restoredEngine.createJob(JobType::Metadata,
                                                 QStringLiteral("读取元数据"),
                                                 QStringLiteral("准备中"));
    QVERIFY(nextId > completedId);

    QSqlQuery query(databaseManager.database());
    query.prepare(QStringLiteral("SELECT state FROM job WHERE id = ?"));
    query.addBindValue(runningId);
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), static_cast<int>(JobState::Failed));
}

void JobEngineTest::projectScopedUpdatesDoNotMutateSameIdInCurrentProject()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    DatabaseManager databaseManager;
    JobEngine engine(&databaseManager);
    QString errorMessage;
    const auto firstPath = QDir(temp.path()).filePath(QStringLiteral("first.cvdb"));
    const auto secondPath = QDir(temp.path()).filePath(QStringLiteral("second.cvdb"));

    QVERIFY2(databaseManager.openProjectDatabase(firstPath, &errorMessage), qPrintable(errorMessage));
    const auto firstId = engine.createJob(JobType::Scan,
                                          QStringLiteral("项目一任务"),
                                          QStringLiteral("运行中"));

    QVERIFY2(databaseManager.openProjectDatabase(secondPath, &errorMessage), qPrintable(errorMessage));
    engine.reloadJobs();
    const auto secondId = engine.createJob(JobType::Scan,
                                           QStringLiteral("项目二任务"),
                                           QStringLiteral("运行中"));
    QCOMPARE(firstId, secondId);

    engine.completeJobForProject(firstPath, firstId, QStringLiteral("项目一已完成"));
    engine.waitForPersistence();

    const auto currentJobs = engine.jobs();
    QCOMPARE(currentJobs.size(), 1);
    QCOMPARE(currentJobs.constFirst().id, secondId);
    QCOMPARE(currentJobs.constFirst().state, JobState::Running);

    QSqlQuery currentQuery(databaseManager.database());
    currentQuery.prepare(QStringLiteral("SELECT state FROM job WHERE id = ?"));
    currentQuery.addBindValue(secondId);
    QVERIFY(currentQuery.exec());
    QVERIFY(currentQuery.next());
    QCOMPARE(currentQuery.value(0).toInt(), static_cast<int>(JobState::Running));

    const auto connectionName = QStringLiteral("job_engine_first_project_test");
    auto firstDatabase = databaseManager.openThreadConnectionForPath(firstPath,
                                                                      connectionName,
                                                                      &errorMessage);
    QVERIFY2(firstDatabase.isOpen(), qPrintable(errorMessage));
    QSqlQuery firstQuery(firstDatabase);
    firstQuery.prepare(QStringLiteral("SELECT state, detail FROM job WHERE id = ?"));
    firstQuery.addBindValue(firstId);
    QVERIFY(firstQuery.exec());
    QVERIFY(firstQuery.next());
    QCOMPARE(firstQuery.value(0).toInt(), static_cast<int>(JobState::Completed));
    QCOMPARE(firstQuery.value(1).toString(), QStringLiteral("项目一已完成"));
    firstQuery = QSqlQuery();
    firstDatabase.close();
    firstDatabase = QSqlDatabase();
    databaseManager.closeThreadConnection(connectionName);
}

void JobEngineTest::reloadJobsCapsLoadedHistory()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DatabaseManager databaseManager;
    QString errorMessage;
    QVERIFY2(databaseManager.openProjectDatabase(
                 QDir(temp.path()).filePath(QStringLiteral("history.cvdb")),
                 &errorMessage),
             qPrintable(errorMessage));

    QSqlQuery insert(databaseManager.database());
    insert.prepare(QStringLiteral(
        "INSERT INTO job (id, type, state, title, progress, source_root_id, started_at, updated_at) "
        "VALUES (?, ?, ?, ?, 100, 0, ?, ?)"));
    const auto timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (int id = 1; id <= 520; ++id) {
        insert.bindValue(0, id);
        insert.bindValue(1, static_cast<int>(JobType::Scan));
        insert.bindValue(2, static_cast<int>(JobState::Completed));
        insert.bindValue(3, QStringLiteral("历史任务 %1").arg(id));
        insert.bindValue(4, timestamp);
        insert.bindValue(5, timestamp);
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    }

    JobEngine engine(&databaseManager);
    engine.reloadJobs();
    QCOMPARE(engine.jobs().size(), 500);
    QCOMPARE(engine.jobs().constFirst().id, 520);
    const auto newId = engine.createJob(JobType::Metadata,
                                        QStringLiteral("新任务"),
                                        QStringLiteral("准备中"));
    QCOMPARE(newId, 521);
}

void JobEngineTest::persistenceFailureDoesNotPublishInMemoryJob()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DatabaseManager databaseManager;
    QString errorMessage;
    QVERIFY2(databaseManager.openProjectDatabase(
                 QDir(temp.path()).filePath(QStringLiteral("readonly.cvdb")),
                 &errorMessage),
             qPrintable(errorMessage));
    QSqlQuery pragma(databaseManager.database());
    QVERIFY2(pragma.exec(QStringLiteral("PRAGMA query_only = ON")),
             qPrintable(pragma.lastError().text()));

    JobEngine engine(&databaseManager);
    QSignalSpy errorSpy(&engine, &JobEngine::persistenceError);
    const auto id = engine.createJob(JobType::Scan,
                                     QStringLiteral("不应发布"),
                                     QStringLiteral("写入失败"));

    QCOMPARE(id, 0);
    QVERIFY(engine.jobs().isEmpty());
    QCOMPARE(errorSpy.count(), 1);
}

void JobEngineTest::progressPersistenceDoesNotBlockUiOnWriterLock()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DatabaseManager databaseManager;
    QString errorMessage;
    const auto databasePath = QDir(temp.path()).filePath(QStringLiteral("async-progress.cvdb"));
    QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));

    JobEngine engine(&databaseManager);
    const auto jobId = engine.createJob(
        JobType::Scan, QStringLiteral("异步进度"), QStringLiteral("准备中"));
    QVERIFY(jobId > 0);

    const auto lockConnectionName = QStringLiteral("job_engine_writer_lock_test");
    auto lockDatabase = databaseManager.openThreadConnectionForPath(
        databasePath, lockConnectionName, &errorMessage);
    QVERIFY2(lockDatabase.isOpen(), qPrintable(errorMessage));
    QVERIFY(lockDatabase.transaction());
    QSqlQuery lockQuery(lockDatabase);
    lockQuery.prepare(QStringLiteral("UPDATE job SET detail = 'locked' WHERE id = ?"));
    lockQuery.addBindValue(jobId);
    QVERIFY(lockQuery.exec());

    QElapsedTimer elapsed;
    elapsed.start();
    engine.updateJob(jobId, 42, QStringLiteral("后台写入"));
    QVERIFY2(elapsed.elapsed() < 100,
             qPrintable(QStringLiteral("UI 更新被数据库锁阻塞 %1ms").arg(elapsed.elapsed())));

    lockDatabase.rollback();
    lockQuery = QSqlQuery();
    lockDatabase.close();
    lockDatabase = QSqlDatabase();
    databaseManager.closeThreadConnection(lockConnectionName);
    engine.waitForPersistence();

    QSqlQuery query(databaseManager.database());
    query.prepare(QStringLiteral("SELECT progress, detail FROM job WHERE id = ?"));
    query.addBindValue(jobId);
    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 42);
    QCOMPARE(query.value(1).toString(), QStringLiteral("后台写入"));
}

QTEST_GUILESS_MAIN(JobEngineTest)

#include "JobEngineTest.moc"
