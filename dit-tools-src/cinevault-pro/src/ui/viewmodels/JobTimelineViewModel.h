#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QVariantList>

class JobListModel;
class JobService;
class VideoAnalysisService;

class JobTimelineViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(JobListModel* model READ model CONSTANT)
    Q_PROPERTY(QVariantList timelineItems READ timelineItems NOTIFY timelineChanged)
    Q_PROPERTY(bool hasBatchSummary READ hasBatchSummary NOTIFY stateChanged)
    Q_PROPERTY(QString batchTitle READ batchTitle NOTIFY stateChanged)
    Q_PROPERTY(QString batchProgressText READ batchProgressText NOTIFY stateChanged)
    Q_PROPERTY(int batchProgress READ batchProgress NOTIFY stateChanged)
    Q_PROPERTY(QString batchStatusText READ batchStatusText NOTIFY stateChanged)
    Q_PROPERTY(QString batchCurrentLabel READ batchCurrentLabel NOTIFY stateChanged)
    Q_PROPERTY(QString batchCurrentDetail READ batchCurrentDetail NOTIFY stateChanged)
    Q_PROPERTY(int batchCurrentProgress READ batchCurrentProgress NOTIFY stateChanged)
    Q_PROPERTY(bool hasActiveBatchItem READ hasActiveBatchItem NOTIFY stateChanged)
    Q_PROPERTY(int batchFinishedCount READ batchFinishedCount NOTIFY stateChanged)
    Q_PROPERTY(int batchSuccessfulCount READ batchSuccessfulCount NOTIFY stateChanged)
    Q_PROPERTY(int batchFailedCount READ batchFailedCount NOTIFY stateChanged)
    Q_PROPERTY(int batchTotalCount READ batchTotalCount NOTIFY stateChanged)
    Q_PROPERTY(int batchQueuedCount READ batchQueuedCount NOTIFY stateChanged)
    Q_PROPERTY(bool canClearCompletedJobs READ canClearCompletedJobs NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    Q_PROPERTY(qint64 selectedJobId READ selectedJobId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobTitle READ selectedJobTitle NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobDetail READ selectedJobDetail NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobTypeLabel READ selectedJobTypeLabel NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobStateLabel READ selectedJobStateLabel NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobError READ selectedJobError NOTIFY stateChanged)
    Q_PROPERTY(int selectedJobProgress READ selectedJobProgress NOTIFY stateChanged)
    Q_PROPERTY(QString selectedSubjectKind READ selectedSubjectKind NOTIFY stateChanged)
    Q_PROPERTY(QString selectedSubjectName READ selectedSubjectName NOTIFY stateChanged)
    Q_PROPERTY(QString selectedSubjectPath READ selectedSubjectPath NOTIFY stateChanged)
    Q_PROPERTY(QString selectedSubjectThumbnailPath READ selectedSubjectThumbnailPath NOTIFY stateChanged)
    Q_PROPERTY(QString selectedSubjectThumbnailStatusLabel READ selectedSubjectThumbnailStatusLabel NOTIFY stateChanged)
    Q_PROPERTY(QString selectedSubjectTypeLabel READ selectedSubjectTypeLabel NOTIFY stateChanged)
    Q_PROPERTY(QString selectedProgressLabel READ selectedProgressLabel NOTIFY stateChanged)
    Q_PROPERTY(QString selectedProgressShortLabel READ selectedProgressShortLabel NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobStartedAt READ selectedJobStartedAt NOTIFY stateChanged)
    Q_PROPERTY(QString selectedJobUpdatedAt READ selectedJobUpdatedAt NOTIFY stateChanged)

public:
    explicit JobTimelineViewModel(JobService *jobService, VideoAnalysisService *videoAnalysisService, QObject *parent = nullptr);

    JobListModel *model() const;
    QVariantList timelineItems() const;
    bool hasBatchSummary() const;
    QString batchTitle() const;
    QString batchProgressText() const;
    int batchProgress() const;
    QString batchStatusText() const;
    QString batchCurrentLabel() const;
    QString batchCurrentDetail() const;
    int batchCurrentProgress() const;
    bool hasActiveBatchItem() const;
    int batchFinishedCount() const;
    int batchSuccessfulCount() const;
    int batchFailedCount() const;
    int batchTotalCount() const;
    int batchQueuedCount() const;
    bool canClearCompletedJobs() const;
    bool hasSelection() const;
    qint64 selectedJobId() const;
    QString selectedJobTitle() const;
    QString selectedJobDetail() const;
    QString selectedJobTypeLabel() const;
    QString selectedJobStateLabel() const;
    QString selectedJobError() const;
    int selectedJobProgress() const;
    QString selectedSubjectKind() const;
    QString selectedSubjectName() const;
    QString selectedSubjectPath() const;
    QString selectedSubjectThumbnailPath() const;
    QString selectedSubjectThumbnailStatusLabel() const;
    QString selectedSubjectTypeLabel() const;
    QString selectedProgressLabel() const;
    QString selectedProgressShortLabel() const;
    QString selectedJobStartedAt() const;
    QString selectedJobUpdatedAt() const;

public slots:
    void reload();
    Q_INVOKABLE void selectJob(qint64 jobId);
    Q_INVOKABLE void clearCompletedJobs();

signals:
    void timelineChanged();
    void stateChanged();

private:
    const Job *selectedJob() const;

    JobService *m_jobService = nullptr;
    VideoAnalysisService *m_videoAnalysisService = nullptr;
    JobListModel *m_model = nullptr;
    QVariantList m_timelineItems;
    QVector<Job> m_jobs;
    qint64 m_selectedJobId = 0;
};
