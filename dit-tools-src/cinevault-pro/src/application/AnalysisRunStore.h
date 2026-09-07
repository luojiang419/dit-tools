#pragma once

#include "domain/Entities.h"

// A rebuild checkpoints beside its new frame images. Active database rows are
// replaced only after summarization succeeds; interrupted runs can reuse work.
class AnalysisRunStore {
public:
    explicit AnalysisRunStore(QString cacheDirectory);
    bool begin(const VisualAnalysisPlan &plan, const QVector<FrameAnalysisRecord> &frames,
               const QString &model, QString *errorMessage);
    bool load(const VisualAnalysisPlan &expected, const QString &model,
              VisualAnalysisPlan *plan, QVector<FrameAnalysisRecord> *frames,
              bool *found, QString *errorMessage);
    bool saveFrame(const FrameAnalysisRecord &frame, QString *errorMessage) const;
    void markPublished() const;
    bool hasPendingRun() const;
    QString runDirectory() const { return m_runDirectory; }

private:
    QString m_cacheDirectory;
    QString m_runDirectory;
};
