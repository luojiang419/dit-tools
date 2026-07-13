#pragma once

#include "domain/Entities.h"

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>

class AppSettings;
class FFmpegAdapter;
class GlobalDatabaseManager;
class JobEngine;
class VisionApiClient;

class VideoAnalysisService : public QObject {
    Q_OBJECT

public:
    explicit VideoAnalysisService(GlobalDatabaseManager *globalDatabaseManager,
                                  JobEngine *jobEngine,
                                  AppSettings *settings,
                                  FFmpegAdapter *ffmpegAdapter,
                                  VisionApiClient *visionApiClient,
                                  QObject *parent = nullptr);

    bool enqueueVideo(const QString &videoKey, QString *errorMessage = nullptr);
    int enqueueVideos(const QStringList &videoKeys, QString *errorMessage = nullptr);
    bool retryFrame(const QString &videoKey, int frameNumber, QString *errorMessage = nullptr);
    int pendingDimensionCount(const QString &videoKey, const QStringList &dimensions, QString *errorMessage = nullptr) const;
    bool analyzeDimensions(const QString &videoKey, const QStringList &dimensions, QString *errorMessage = nullptr);
    bool hasBatchSummary() const;
    int batchTotalCount() const;
    int batchFinishedCount() const;
    int batchFailedCount() const;
    int batchSuccessfulCount() const;
    int batchQueuedCount() const;
    qint64 batchProgressPercent() const;
    qint64 batchCurrentProgressPercent() const;
    QString batchCurrentLabel() const;
    QString batchCurrentDetail() const;
    QString batchStatusText() const;

public slots:
    void analyzeVideo(const QString &videoKey);
    bool confirmVideo(const QString &videoKey);
    int confirmVideos(const QStringList &videoKeys);

signals:
    void catalogChanged();
    void analysisProgressChanged(const QString &videoKey,
                                 qint64 progress,
                                 const QString &detail,
                                 int state,
                                 const QString &errorMessage);
    void analysisQueueChanged(const QString &currentVideoKey, int queuedCount);
    void analysisBatchChanged();
    void dimensionAnalysisProgressChanged(const QString &videoKey,
                                          bool running,
                                          const QString &detail,
                                          const QString &errorMessage);

private:
    enum class BatchItemState {
        Queued = 0,
        Running,
        Completed,
        Failed
    };

    struct DimensionAnalysisJob {
        QString videoKey;
        QStringList dimensions;
    };

    bool validateReadyForEnqueue(const QString &videoKey, QString *errorMessage) const;
    bool enqueueJob(const AnalysisJob &job, QString *errorMessage);
    bool startDimensionAnalysisNow(const QString &videoKey, const QStringList &dimensions, QString *errorMessage);
    void startNextAnalysis();
    void startNextDimensionAnalysis();
    void finishCurrentAnalysis(const QString &videoKey);
    void reportAnalysisProgress(const QString &videoKey,
                                qint64 progress,
                                const QString &detail,
                                JobState state,
                                const QString &errorMessage = QString());
    void resetBatchSummaryIfIdle();
    void ensureBatchItem(const QString &videoKey);
    void setBatchItemState(const QString &videoKey, BatchItemState state);
    void notifyBatchChanged();
    QString lookupVideoLabel(const QString &videoKey) const;
    void updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext = JobProgressContext());
    void updateJobSubject(qint64 jobId, const JobSubject &subject);
    void completeJob(qint64 jobId, const QString &detail);
    void failJob(qint64 jobId, const QString &errorMessage);
    void notifyCatalogChanged();

    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    JobEngine *m_jobEngine = nullptr;
    AppSettings *m_settings = nullptr;
    FFmpegAdapter *m_ffmpegAdapter = nullptr;
    VisionApiClient *m_visionApiClient = nullptr;
    QQueue<AnalysisJob> m_analysisQueue;
    QQueue<DimensionAnalysisJob> m_dimensionAnalysisQueue;
    QSet<QString> m_queuedVideoKeys;
    QSet<QString> m_dimensionAnalysisKeys;
    AnalysisJob m_currentJob;
    QString m_currentVideoKey;
    bool m_analysisRunning = false;
    QHash<QString, BatchItemState> m_batchStates;
    QHash<QString, QString> m_batchLabels;
    qint64 m_batchCurrentProgress = 0;
    QString m_batchCurrentDetail;
};
