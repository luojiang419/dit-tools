#include "ui/viewmodels/JobTimelineViewModel.h"

#include "application/JobService.h"
#include "application/VideoAnalysisService.h"
#include "core/jobs/JobEngine.h"
#include "shared/Formatters.h"
#include "ui/models/JobListModel.h"

#include <QTimer>

namespace {
bool hasAnalysisBatchSummary(const VideoAnalysisService *service)
{
    return service && service->hasBatchSummary();
}

bool isFinishedJobState(JobState state)
{
    return state == JobState::Completed || state == JobState::Failed || state == JobState::Cancelled;
}

int countJobs(const QVector<Job> &jobs, JobState state)
{
    int count = 0;
    for (const auto &job : jobs) {
        if (job.state == state) {
            ++count;
        }
    }
    return count;
}

int finishedJobCount(const QVector<Job> &jobs)
{
    int count = 0;
    for (const auto &job : jobs) {
        if (isFinishedJobState(job.state)) {
            ++count;
        }
    }
    return count;
}

int aggregateJobProgress(const QVector<Job> &jobs)
{
    if (jobs.isEmpty()) {
        return 0;
    }
    qint64 total = 0;
    for (const auto &job : jobs) {
        total += qBound<qint64>(qint64{0}, job.progress, qint64{100});
    }
    return static_cast<int>(total / jobs.size());
}

const Job *activeSummaryJob(const QVector<Job> &jobs)
{
    for (const auto &job : jobs) {
        if (job.state == JobState::Running) {
            return &job;
        }
    }
    for (const auto &job : jobs) {
        if (job.state == JobState::Pending) {
            return &job;
        }
    }
    return jobs.isEmpty() ? nullptr : &jobs.first();
}

QString jobDisplayName(const Job &job)
{
    const auto subjectName = job.subject.name.trimmed();
    return subjectName.isEmpty() ? job.title : subjectName;
}

QString jobSummaryText(const QVector<Job> &jobs)
{
    if (jobs.isEmpty()) {
        return QStringLiteral("暂无任务。");
    }

    const auto running = countJobs(jobs, JobState::Running);
    const auto pending = countJobs(jobs, JobState::Pending);
    const auto failed = countJobs(jobs, JobState::Failed);
    const auto completed = countJobs(jobs, JobState::Completed);

    if (running > 0) {
        return QStringLiteral("当前有 %1 个任务正在处理，%2 个任务等待处理。").arg(running).arg(pending);
    }
    if (pending > 0) {
        return QStringLiteral("当前有 %1 个任务等待处理。").arg(pending);
    }
    if (failed > 0) {
        return QStringLiteral("任务已处理 %1/%2 个，其中 %3 个失败。").arg(completed + failed).arg(jobs.size()).arg(failed);
    }
    return QStringLiteral("任务已全部处理完成。");
}

bool isBatchOwnedJob(const Job &job)
{
    return job.type == JobType::ContentAnalysis;
}

bool includeInFooter(const Job &job, bool hasAnalysisBatch)
{
    return !hasAnalysisBatch || !isBatchOwnedJob(job);
}

int activeJobCount(const QVector<Job> &jobs, bool hasAnalysisBatch)
{
    int count = 0;
    for (const auto &job : jobs) {
        if (includeInFooter(job, hasAnalysisBatch)
            && (job.state == JobState::Running || job.state == JobState::Pending)) {
            ++count;
        }
    }
    return count;
}

QString thumbnailStatusLabel(ThumbnailStatus status)
{
    switch (status) {
    case ThumbnailStatus::Success: return QStringLiteral("缩略图已生成，正在等待路径同步。");
    case ThumbnailStatus::Running: return QStringLiteral("缩略图生成中，完成后会自动显示。");
    case ThumbnailStatus::Failed: return QStringLiteral("缩略图生成失败，可重新导入或等待重试。");
    case ThumbnailStatus::Pending: return QStringLiteral("缩略图等待生成。");
    }
    return QStringLiteral("该任务暂无可显示缩略图。");
}
}

JobTimelineViewModel::JobTimelineViewModel(JobService *jobService, VideoAnalysisService *videoAnalysisService, QObject *parent)
    : QObject(parent)
    , m_jobService(jobService)
    , m_videoAnalysisService(videoAnalysisService)
    , m_model(new JobListModel(this))
{
    connect(m_jobService->engine(), &JobEngine::jobsChanged, this, &JobTimelineViewModel::scheduleReload);
    if (m_videoAnalysisService) {
        connect(m_videoAnalysisService, &VideoAnalysisService::analysisBatchChanged, this, &JobTimelineViewModel::scheduleReload);
    }
    reload();
}

JobListModel *JobTimelineViewModel::model() const
{
    return m_model;
}

QVariantList JobTimelineViewModel::timelineItems() const
{
    return m_timelineItems;
}

bool JobTimelineViewModel::footerHasTasks() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService) || !m_jobs.isEmpty();
}

int JobTimelineViewModel::footerProgress() const
{
    const auto hasAnalysisBatch = hasAnalysisBatchSummary(m_videoAnalysisService);
    qint64 progressSum = 0;
    qint64 taskCount = 0;
    if (hasAnalysisBatch) {
        const auto batchTotal = m_videoAnalysisService->batchTotalCount();
        if (batchTotal > 0) {
            progressSum += batchTotal * m_videoAnalysisService->batchProgressPercent();
            taskCount += batchTotal;
        }
    }
    for (const auto &job : m_jobs) {
        if (!includeInFooter(job, hasAnalysisBatch)
            || (job.state != JobState::Running && job.state != JobState::Pending)) {
            continue;
        }
        progressSum += qBound<qint64>(qint64{0}, job.progress, qint64{100});
        ++taskCount;
    }
    if (taskCount > 0) {
        return static_cast<int>(progressSum / taskCount);
    }
    if (hasAnalysisBatch) {
        return static_cast<int>(m_videoAnalysisService->batchProgressPercent());
    }
    return m_jobs.isEmpty() ? 0 : aggregateJobProgress(m_jobs);
}

QString JobTimelineViewModel::footerProgressText() const
{
    if (!footerHasTasks()) {
        return QStringLiteral("暂无后台任务");
    }
    const auto active = activeJobCount(m_jobs, hasAnalysisBatchSummary(m_videoAnalysisService));
    const auto batchTasks = hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchTotalCount()
        : 0;
    const auto total = active + batchTasks;
    return total > 0
        ? QStringLiteral("%1 项任务 · %2%").arg(total).arg(footerProgress())
        : QStringLiteral("最近任务 · %1%").arg(footerProgress());
}

QString JobTimelineViewModel::footerSummary() const
{
    if (!footerHasTasks()) {
        return QStringLiteral("当前没有后台任务。添加素材源后会在此显示索引与解析进度。");
    }
    const auto running = footerRunningCount();
    const auto pending = footerPendingCount();
    if (running > 0) {
        const auto label = hasAnalysisBatchSummary(m_videoAnalysisService)
            ? m_videoAnalysisService->batchCurrentLabel()
            : batchCurrentLabel();
        return QStringLiteral("正在处理：%1 · 运行 %2 项，等待 %3 项")
            .arg(label.trimmed().isEmpty() ? QStringLiteral("后台任务") : label)
            .arg(running)
            .arg(pending);
    }
    if (pending > 0) {
        return QStringLiteral("当前没有运行中的任务，%1 项任务正在等待处理。").arg(pending);
    }
    if (footerFailedCount() > 0) {
        return QStringLiteral("任务已停止，%1 项需要在任务页查看原因。").arg(footerFailedCount());
    }
    return QStringLiteral("所有任务已完成，可继续添加素材或发起解析。");
}

int JobTimelineViewModel::footerRunningCount() const
{
    const auto hasAnalysisBatch = hasAnalysisBatchSummary(m_videoAnalysisService);
    int running = 0;
    for (const auto &job : m_jobs) {
        if (includeInFooter(job, hasAnalysisBatch) && job.state == JobState::Running) {
            ++running;
        }
    }
    if (hasAnalysisBatch) {
        running += qMax(0,
                         m_videoAnalysisService->batchTotalCount()
                             - m_videoAnalysisService->batchFinishedCount()
                             - m_videoAnalysisService->batchQueuedCount());
    }
    return running;
}

int JobTimelineViewModel::footerPendingCount() const
{
    const auto hasAnalysisBatch = hasAnalysisBatchSummary(m_videoAnalysisService);
    int pending = 0;
    for (const auto &job : m_jobs) {
        if (includeInFooter(job, hasAnalysisBatch) && job.state == JobState::Pending) {
            ++pending;
        }
    }
    return pending + (hasAnalysisBatch ? m_videoAnalysisService->batchQueuedCount() : 0);
}

int JobTimelineViewModel::footerFailedCount() const
{
    const auto hasAnalysisBatch = hasAnalysisBatchSummary(m_videoAnalysisService);
    int failed = 0;
    for (const auto &job : m_jobs) {
        if (includeInFooter(job, hasAnalysisBatch) && job.state == JobState::Failed) {
            ++failed;
        }
    }
    return failed + (hasAnalysisBatch ? m_videoAnalysisService->batchFailedCount() : 0);
}

bool JobTimelineViewModel::hasBatchSummary() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService) || !m_jobs.isEmpty();
}

QString JobTimelineViewModel::batchTitle() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? QStringLiteral("视频解析总量进度")
        : QStringLiteral("任务总量进度");
}

QString JobTimelineViewModel::batchProgressText() const
{
    if (!hasBatchSummary()) {
        return QStringLiteral("暂无任务");
    }
    return QStringLiteral("%1/%2 · %3%")
        .arg(batchFinishedCount())
        .arg(batchTotalCount())
        .arg(batchProgress());
}

int JobTimelineViewModel::batchProgress() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? static_cast<int>(m_videoAnalysisService->batchProgressPercent())
        : aggregateJobProgress(m_jobs);
}

QString JobTimelineViewModel::batchStatusText() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchStatusText()
        : jobSummaryText(m_jobs);
}

QString JobTimelineViewModel::batchCurrentLabel() const
{
    if (hasAnalysisBatchSummary(m_videoAnalysisService)) {
        return m_videoAnalysisService->batchCurrentLabel();
    }
    const auto *job = activeSummaryJob(m_jobs);
    return job ? jobDisplayName(*job) : QString();
}

QString JobTimelineViewModel::batchCurrentDetail() const
{
    if (hasAnalysisBatchSummary(m_videoAnalysisService)) {
        return m_videoAnalysisService->batchCurrentDetail();
    }
    const auto *job = activeSummaryJob(m_jobs);
    if (!job) {
        return QStringLiteral("当前没有正在处理的任务。");
    }
    if (job->state == JobState::Pending) {
        return QStringLiteral("等待处理：%1").arg(job->title);
    }
    return job->detail.trimmed().isEmpty() ? job->title : job->detail;
}

int JobTimelineViewModel::batchCurrentProgress() const
{
    if (hasAnalysisBatchSummary(m_videoAnalysisService)) {
        return static_cast<int>(m_videoAnalysisService->batchCurrentProgressPercent());
    }
    const auto *job = activeSummaryJob(m_jobs);
    return job ? static_cast<int>(job->progress) : 0;
}

bool JobTimelineViewModel::hasActiveBatchItem() const
{
    if (hasAnalysisBatchSummary(m_videoAnalysisService)) {
        return !m_videoAnalysisService->batchCurrentLabel().trimmed().isEmpty();
    }
    const auto *job = activeSummaryJob(m_jobs);
    return job && (job->state == JobState::Running || job->state == JobState::Pending);
}

int JobTimelineViewModel::batchFinishedCount() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchFinishedCount()
        : finishedJobCount(m_jobs);
}

int JobTimelineViewModel::batchSuccessfulCount() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchSuccessfulCount()
        : countJobs(m_jobs, JobState::Completed);
}

int JobTimelineViewModel::batchFailedCount() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchFailedCount()
        : countJobs(m_jobs, JobState::Failed);
}

int JobTimelineViewModel::batchTotalCount() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchTotalCount()
        : m_jobs.size();
}

int JobTimelineViewModel::batchQueuedCount() const
{
    return hasAnalysisBatchSummary(m_videoAnalysisService)
        ? m_videoAnalysisService->batchQueuedCount()
        : countJobs(m_jobs, JobState::Pending);
}

bool JobTimelineViewModel::canClearCompletedJobs() const
{
    return countJobs(m_jobs, JobState::Completed) > 0;
}

bool JobTimelineViewModel::hasSelection() const
{
    return selectedJob() != nullptr;
}

qint64 JobTimelineViewModel::selectedJobId() const
{
    return m_selectedJobId;
}

QString JobTimelineViewModel::selectedJobTitle() const
{
    const auto *job = selectedJob();
    return job ? job->title : QString();
}

QString JobTimelineViewModel::selectedJobDetail() const
{
    const auto *job = selectedJob();
    return job ? job->detail : QStringLiteral("选择左侧任务查看详情。");
}

QString JobTimelineViewModel::selectedJobTypeLabel() const
{
    const auto *job = selectedJob();
    return job ? Formatters::jobTypeLabel(job->type) : QString();
}

QString JobTimelineViewModel::selectedJobStateLabel() const
{
    const auto *job = selectedJob();
    return job ? Formatters::jobStateLabel(job->state) : QString();
}

QString JobTimelineViewModel::selectedJobError() const
{
    const auto *job = selectedJob();
    return job ? job->errorMessage : QString();
}

int JobTimelineViewModel::selectedJobProgress() const
{
    const auto *job = selectedJob();
    return job ? static_cast<int>(job->progress) : 0;
}

QString JobTimelineViewModel::selectedSubjectKind() const
{
    const auto *job = selectedJob();
    return job ? job->subject.kind : QString();
}

QString JobTimelineViewModel::selectedSubjectName() const
{
    const auto *job = selectedJob();
    if (!job) {
        return QString();
    }
    const auto name = job->subject.name.trimmed();
    return name.isEmpty() ? job->title : name;
}

QString JobTimelineViewModel::selectedSubjectPath() const
{
    const auto *job = selectedJob();
    return job ? job->subject.path : QString();
}

QString JobTimelineViewModel::selectedSubjectThumbnailPath() const
{
    const auto *job = selectedJob();
    return job ? job->subject.thumbnailPath : QString();
}

QString JobTimelineViewModel::selectedSubjectThumbnailStatusLabel() const
{
    const auto *job = selectedJob();
    if (!job) {
        return QStringLiteral("点击任务卡片后显示素材或对象详情");
    }
    if (!job->subject.thumbnailPath.trimmed().isEmpty()) {
        return QStringLiteral("缩略图已生成。");
    }
    return thumbnailStatusLabel(job->subject.thumbnailStatus);
}

QString JobTimelineViewModel::selectedSubjectTypeLabel() const
{
    const auto *job = selectedJob();
    if (!job) {
        return QString();
    }
    const auto label = job->subject.typeLabel.trimmed();
    return label.isEmpty() ? Formatters::jobTypeLabel(job->type) : label;
}

QString JobTimelineViewModel::selectedProgressLabel() const
{
    const auto *job = selectedJob();
    return job ? Formatters::jobProgressLabel(job->progressContext) : QString();
}

QString JobTimelineViewModel::selectedProgressShortLabel() const
{
    const auto *job = selectedJob();
    return job ? Formatters::jobProgressShortLabel(job->progressContext) : QString();
}

QString JobTimelineViewModel::selectedJobStartedAt() const
{
    const auto *job = selectedJob();
    return job ? job->startedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
}

QString JobTimelineViewModel::selectedJobUpdatedAt() const
{
    const auto *job = selectedJob();
    return job ? job->updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
}

void JobTimelineViewModel::reload()
{
    auto jobs = m_jobService->engine()->jobs();
    JobListModel::sortForDisplay(jobs);
    m_jobs = jobs;
    m_model->setItems(jobs);

    QVariantList items;
    for (int i = 0; i < jobs.size() && i < 5; ++i) {
        QVariantMap row;
        row.insert(QStringLiteral("title"), jobs.at(i).title);
        row.insert(QStringLiteral("detail"), jobs.at(i).detail);
        row.insert(QStringLiteral("progress"), jobs.at(i).progress);
        row.insert(QStringLiteral("stateLabel"), Formatters::jobStateLabel(jobs.at(i).state));
        row.insert(QStringLiteral("subjectName"), jobs.at(i).subject.name.trimmed().isEmpty() ? jobs.at(i).title : jobs.at(i).subject.name);
        row.insert(QStringLiteral("progressLabel"), Formatters::jobProgressShortLabel(jobs.at(i).progressContext));
        items.append(row);
    }
    m_timelineItems = items;
    if (!m_jobs.isEmpty()) {
        bool found = false;
        for (const auto &job : m_jobs) {
            if (job.id == m_selectedJobId) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_selectedJobId = m_jobs.first().id;
        }
    } else {
        m_selectedJobId = 0;
    }
    emit timelineChanged();
    emit stateChanged();
}

void JobTimelineViewModel::scheduleReload()
{
    if (m_reloadScheduled) {
        return;
    }
    m_reloadScheduled = true;
    QTimer::singleShot(120, this, [this]() {
        m_reloadScheduled = false;
        reload();
    });
}

void JobTimelineViewModel::selectJob(qint64 jobId)
{
    if (m_selectedJobId == jobId) {
        return;
    }
    m_selectedJobId = jobId;
    emit stateChanged();
}

void JobTimelineViewModel::clearCompletedJobs()
{
    if (!m_jobService || !m_jobService->engine()) {
        return;
    }
    m_jobService->engine()->clearCompletedJobs();
}

const Job *JobTimelineViewModel::selectedJob() const
{
    for (const auto &job : m_jobs) {
        if (job.id == m_selectedJobId) {
            return &job;
        }
    }
    return nullptr;
}
