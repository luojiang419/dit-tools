#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

struct ProcessMemorySample {
    qint64 workingSetBytes = -1;
    qint64 privateBytes = -1;
};

class PerformanceTelemetry final {
public:
    static PerformanceTelemetry &global();

    qint64 beginStage(const QString &component,
                      const QString &stage,
                      const QJsonObject &fields = {});
    void finishStage(const QString &component,
                     const QString &stage,
                     qint64 startedAtMs,
                     const QString &status = QStringLiteral("ok"),
                     const QJsonObject &fields = {});
    void recordBatch(const QString &component,
                     const QString &stage,
                     qint64 itemCount,
                     qint64 elapsedMs);
    void setQueueDepth(const QString &queueName, qint64 depth);
    void recordSqliteBusy(const QString &component);
    void recordUiHeartbeat(qint64 delayMs);
    void logSnapshot(const QString &reason);

    [[nodiscard]] QJsonObject snapshot() const;
    [[nodiscard]] static ProcessMemorySample sampleProcessMemory();
    void resetForTesting();

private:
    PerformanceTelemetry();

    void writeEvent(const QString &eventName, QJsonObject fields);
    void updateMemoryPeakLocked(const ProcessMemorySample &sample);

    mutable QMutex m_mutex;
    QElapsedTimer m_monotonicClock;
    QHash<QString, qint64> m_currentQueueDepths;
    QHash<QString, qint64> m_peakQueueDepths;
    QHash<QString, qint64> m_stageBatchCounts;
    QHash<QString, qint64> m_stageItemCounts;
    QHash<QString, qint64> m_stageElapsedMs;
    QVector<qint64> m_uiHeartbeatDelays;
    qint64 m_sqliteBusyCount = 0;
    qint64 m_peakWorkingSetBytes = -1;
    qint64 m_peakPrivateBytes = -1;
};

class UiHeartbeatMonitor final : public QObject {
    Q_OBJECT

public:
    explicit UiHeartbeatMonitor(PerformanceTelemetry *telemetry,
                                QObject *parent = nullptr);

    void start();
    void stop();

private:
    void sampleHeartbeat();

    static constexpr int HeartbeatIntervalMs = 250;
    static constexpr int SnapshotIntervalSamples = 20;

    PerformanceTelemetry *m_telemetry = nullptr;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    int m_samplesSinceSnapshot = 0;
};
