#pragma once

#include "shared/VisualAnalysisMetadata.h"
#include <QSet>

class FrameRequestSchedule {
public:
    static constexpr int MaximumConcurrency = 8;

    QVector<int> takeNext(const QVector<FrameAnalysisRecord> &frames, int limit, int profileVersion)
    {
        QVector<int> batch;
        limit = qBound(1, limit, MaximumConcurrency);
        for (int index = 0; index < frames.size() && batch.size() < limit; ++index) {
            if (!m_dispatched.contains(index)
                && !VisualAnalysisMetadata::isFrameAnalysisComplete(frames.at(index), profileVersion)) {
                batch.append(index);
                m_dispatched.insert(index);
            }
        }
        return batch;
    }

private:
    QSet<int> m_dispatched;
};
