#include "ui/models/JobListModel.h"

#include <QtTest>

namespace {
Job makeFrameJob()
{
    Job job;
    job.id = 10;
    job.type = JobType::ContentAnalysis;
    job.state = JobState::Running;
    job.title = QStringLiteral("素材内容解析");
    job.detail = QStringLiteral("正在解析视频帧");
    job.progress = 31;
    job.subject.kind = QStringLiteral("asset");
    job.subject.key = QStringLiteral("video-key");
    job.subject.name = QStringLiteral("A001_C001.mov");
    job.subject.path = QStringLiteral("D:/media/A001_C001.mov");
    job.subject.thumbnailPath = QStringLiteral("D:/cache/A001_C001.jpg");
    job.subject.typeLabel = QStringLiteral("视频");
    job.progressContext.currentStep = 2;
    job.progressContext.totalSteps = 4;
    job.progressContext.stepLabel = QStringLiteral("解析视频帧");
    job.progressContext.currentItem = 37;
    job.progressContext.totalItems = 120;
    job.progressContext.unitLabel = QStringLiteral("帧");
    job.progressContext.currentFrameNumber = 240;
    return job;
}

Job makeFileJob()
{
    Job job;
    job.id = 11;
    job.type = JobType::Thumbnail;
    job.state = JobState::Running;
    job.title = QStringLiteral("生成缩略图素材源");
    job.detail = QStringLiteral("已生成 8/20 张缩略图");
    job.progress = 40;
    job.subject.kind = QStringLiteral("sourceRoot");
    job.subject.name = QStringLiteral("素材源");
    job.subject.typeLabel = QStringLiteral("素材源");
    job.progressContext.currentStep = 1;
    job.progressContext.totalSteps = 1;
    job.progressContext.stepLabel = QStringLiteral("生成缩略图");
    job.progressContext.currentItem = 8;
    job.progressContext.totalItems = 20;
    job.progressContext.unitLabel = QStringLiteral("张");
    return job;
}

Job makeOrderedJob(qint64 id, JobState state)
{
    Job job;
    job.id = id;
    job.type = JobType::ContentAnalysis;
    job.state = state;
    job.title = QStringLiteral("Job %1").arg(id);
    job.updatedAt = QDateTime::fromString(QStringLiteral("2026-07-06T12:00:%1").arg(id, 2, 10, QLatin1Char('0')), Qt::ISODate);
    return job;
}
}

class JobListModelTest : public QObject {
    Q_OBJECT

private slots:
    void exposesFrameProgressRoles();
    void exposesNonFrameProgressRoles();
    void sortsRunningJobsFirst();
};

void JobListModelTest::exposesFrameProgressRoles()
{
    JobListModel model;
    model.setItems({makeFrameJob()});

    const auto index = model.index(0, 0);
    QVERIFY(index.isValid());
    QCOMPARE(model.data(index, JobListModel::SubjectNameRole).toString(), QStringLiteral("A001_C001.mov"));
    QCOMPARE(model.data(index, JobListModel::SubjectThumbnailPathRole).toString(), QStringLiteral("D:/cache/A001_C001.jpg"));
    QCOMPARE(model.data(index, JobListModel::SubjectTypeLabelRole).toString(), QStringLiteral("视频"));
    QCOMPARE(model.data(index, JobListModel::ProgressLabelRole).toString(),
             QStringLiteral("第2/4步 · 解析视频帧 · 第37/120帧 · 当前帧 240"));
    QCOMPARE(model.data(index, JobListModel::ProgressShortLabelRole).toString(),
             QStringLiteral("第2/4步 · 第37/120帧 · 帧 240"));
}

void JobListModelTest::exposesNonFrameProgressRoles()
{
    JobListModel model;
    model.setItems({makeFileJob()});

    const auto index = model.index(0, 0);
    QVERIFY(index.isValid());
    QCOMPARE(model.data(index, JobListModel::JobTypeLabelRole).toString(), QStringLiteral("缩略图"));
    QCOMPARE(model.data(index, JobListModel::ProgressLabelRole).toString(),
             QStringLiteral("第1/1步 · 生成缩略图 · 8/20张"));
    QCOMPARE(model.data(index, JobListModel::ProgressShortLabelRole).toString(),
             QStringLiteral("第1/1步 · 8/20张"));
}

void JobListModelTest::sortsRunningJobsFirst()
{
    JobListModel model;
    model.setItems({
        makeOrderedJob(1, JobState::Completed),
        makeOrderedJob(2, JobState::Pending),
        makeOrderedJob(3, JobState::Running),
        makeOrderedJob(4, JobState::Failed)
    });

    QCOMPARE(model.data(model.index(0, 0), JobListModel::IdRole).toLongLong(), qint64{3});
    QCOMPARE(model.data(model.index(1, 0), JobListModel::IdRole).toLongLong(), qint64{2});
    QCOMPARE(model.data(model.index(2, 0), JobListModel::IdRole).toLongLong(), qint64{4});
    QCOMPARE(model.data(model.index(3, 0), JobListModel::IdRole).toLongLong(), qint64{1});
}

QTEST_APPLESS_MAIN(JobListModelTest)

#include "JobListModelTest.moc"
