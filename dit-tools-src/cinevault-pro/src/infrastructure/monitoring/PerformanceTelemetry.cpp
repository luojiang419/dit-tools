#include "infrastructure/monitoring/PerformanceTelemetry.h"

#include "infrastructure/logging/Logger.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QMutexLocker>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace {
QJsonObject hashToJson(const QHash<QString, qint64> &values)
{
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}

qint64 percentile95(QVector<qint64> values)
{
    if (values.isEmpty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto index = qBound<qsizetype>(
        0,
        (values.size() * 95 + 99) / 100 - 1,
        values.size() - 1);
    return values.at(index);
}
}

PerformanceTelemetry &PerformanceTelemetry::global()
{
    static PerformanceTelemetry telemetry;
    return telemetry;
}

PerformanceTelemetry::PerformanceTelemetry()
{
    m_monotonicClock.start();
}

qint64 PerformanceTelemetry::beginStage(const QString &component,
                                        const QString &stage,
                                        const QJsonObject &fields)
{
    QJsonObject event = fields;
    event.insert(QStringLiteral("component"), component);
    event.insert(QStringLiteral("stage"), stage);
    writeEvent(QStringLiteral("stage_started"), event);
    return m_monotonicClock.elapsed();
}

void PerformanceTelemetry::finishStage(const QString &component,
                                       const QString &stage,
                                       qint64 startedAtMs,
                                       const QString &status,
                                       const QJsonObject &fields)
{
    const auto elapsedMs = qMax<qint64>(0, m_monotonicClock.elapsed() - startedAtMs);
    QJsonObject event = fields;
    event.insert(QStringLiteral("component"), component);
    event.insert(QStringLiteral("stage"), stage);
    event.insert(QStringLiteral("status"), status);
    event.insert(QStringLiteral("elapsed_ms"), elapsedMs);
    writeEvent(QStringLiteral("stage_finished"), event);
}

void PerformanceTelemetry::recordBatch(const QString &component,
                                       const QString &stage,
                                       qint64 itemCount,
                                       qint64 elapsedMs)
{
    const auto key = component + QLatin1Char('.') + stage;
    const auto memory = sampleProcessMemory();
    QMutexLocker locker(&m_mutex);
    ++m_stageBatchCounts[key];
    m_stageItemCounts[key] += qMax<qint64>(0, itemCount);
    m_stageElapsedMs[key] += qMax<qint64>(0, elapsedMs);
    updateMemoryPeakLocked(memory);
}

void PerformanceTelemetry::setQueueDepth(const QString &queueName, qint64 depth)
{
    const auto boundedDepth = qMax<qint64>(0, depth);
    const auto memory = sampleProcessMemory();
    QMutexLocker locker(&m_mutex);
    m_currentQueueDepths.insert(queueName, boundedDepth);
    m_peakQueueDepths.insert(
        queueName,
        qMax(m_peakQueueDepths.value(queueName, 0), boundedDepth));
    updateMemoryPeakLocked(memory);
}

void PerformanceTelemetry::recordSqliteBusy(const QString &component)
{
    {
        QMutexLocker locker(&m_mutex);
        ++m_sqliteBusyCount;
    }
    writeEvent(QStringLiteral("sqlite_busy"), {
        {QStringLiteral("component"), component}
    });
}

void PerformanceTelemetry::recordUiHeartbeat(qint64 delayMs)
{
    const auto memory = sampleProcessMemory();
    QMutexLocker locker(&m_mutex);
    m_uiHeartbeatDelays.append(qMax<qint64>(0, delayMs));
    constexpr qsizetype MaxHeartbeatSamples = 2048;
    if (m_uiHeartbeatDelays.size() > MaxHeartbeatSamples) {
        m_uiHeartbeatDelays.remove(0, m_uiHeartbeatDelays.size() - MaxHeartbeatSamples);
    }
    updateMemoryPeakLocked(memory);
}

void PerformanceTelemetry::logSnapshot(const QString &reason)
{
    auto event = snapshot();
    event.insert(QStringLiteral("reason"), reason);
    writeEvent(QStringLiteral("performance_snapshot"), event);
}

QJsonObject PerformanceTelemetry::snapshot() const
{
    const auto memory = sampleProcessMemory();
    QMutexLocker locker(&m_mutex);
    const_cast<PerformanceTelemetry *>(this)->updateMemoryPeakLocked(memory);

    qint64 maxHeartbeatDelayMs = 0;
    for (const auto delay : m_uiHeartbeatDelays) {
        maxHeartbeatDelayMs = qMax(maxHeartbeatDelayMs, delay);
    }

    return {
        {QStringLiteral("working_set_bytes"), memory.workingSetBytes},
        {QStringLiteral("private_bytes"), memory.privateBytes},
        {QStringLiteral("peak_working_set_bytes"), m_peakWorkingSetBytes},
        {QStringLiteral("peak_private_bytes"), m_peakPrivateBytes},
        {QStringLiteral("sqlite_busy_count"), m_sqliteBusyCount},
        {QStringLiteral("ui_heartbeat_samples"), m_uiHeartbeatDelays.size()},
        {QStringLiteral("ui_heartbeat_p95_ms"), percentile95(m_uiHeartbeatDelays)},
        {QStringLiteral("ui_heartbeat_max_ms"), maxHeartbeatDelayMs},
        {QStringLiteral("queue_depths"), hashToJson(m_currentQueueDepths)},
        {QStringLiteral("peak_queue_depths"), hashToJson(m_peakQueueDepths)},
        {QStringLiteral("stage_batch_counts"), hashToJson(m_stageBatchCounts)},
        {QStringLiteral("stage_item_counts"), hashToJson(m_stageItemCounts)},
        {QStringLiteral("stage_elapsed_ms"), hashToJson(m_stageElapsedMs)}
    };
}

ProcessMemorySample PerformanceTelemetry::sampleProcessMemory()
{
    ProcessMemorySample sample;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        sample.workingSetBytes = static_cast<qint64>(counters.WorkingSetSize);
        sample.privateBytes = static_cast<qint64>(counters.PrivateUsage);
    }
#endif
    return sample;
}

void PerformanceTelemetry::resetForTesting()
{
    QMutexLocker locker(&m_mutex);
    m_currentQueueDepths.clear();
    m_peakQueueDepths.clear();
    m_stageBatchCounts.clear();
    m_stageItemCounts.clear();
    m_stageElapsedMs.clear();
    m_uiHeartbeatDelays.clear();
    m_sqliteBusyCount = 0;
    m_peakWorkingSetBytes = -1;
    m_peakPrivateBytes = -1;
}

void PerformanceTelemetry::writeEvent(const QString &eventName, QJsonObject fields)
{
    fields.insert(QStringLiteral("event"), eventName);
    fields.insert(
        QStringLiteral("timestamp"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    Logger::info(
        QStringLiteral("telemetry=%1")
            .arg(QString::fromUtf8(QJsonDocument(fields).toJson(QJsonDocument::Compact))));
}

void PerformanceTelemetry::updateMemoryPeakLocked(const ProcessMemorySample &sample)
{
    if (sample.workingSetBytes >= 0) {
        m_peakWorkingSetBytes = qMax(m_peakWorkingSetBytes, sample.workingSetBytes);
    }
    if (sample.privateBytes >= 0) {
        m_peakPrivateBytes = qMax(m_peakPrivateBytes, sample.privateBytes);
    }
}

UiHeartbeatMonitor::UiHeartbeatMonitor(PerformanceTelemetry *telemetry, QObject *parent)
    : QObject(parent)
    , m_telemetry(telemetry)
{
    m_timer.setInterval(HeartbeatIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        sampleHeartbeat();
    });
}

void UiHeartbeatMonitor::start()
{
    if (m_timer.isActive()) {
        return;
    }
    m_samplesSinceSnapshot = 0;
    m_elapsed.start();
    m_timer.start();
}

void UiHeartbeatMonitor::stop()
{
    m_timer.stop();
}

void UiHeartbeatMonitor::sampleHeartbeat()
{
    if (!m_telemetry || !m_elapsed.isValid()) {
        return;
    }
    const auto elapsedMs = m_elapsed.restart();
    m_telemetry->recordUiHeartbeat(qMax<qint64>(0, elapsedMs - HeartbeatIntervalMs));
    ++m_samplesSinceSnapshot;
    if (m_samplesSinceSnapshot >= SnapshotIntervalSamples) {
        m_samplesSinceSnapshot = 0;
        m_telemetry->logSnapshot(QStringLiteral("ui_heartbeat"));
    }
}
