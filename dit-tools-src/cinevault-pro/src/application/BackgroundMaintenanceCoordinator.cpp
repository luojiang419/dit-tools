#include "application/BackgroundMaintenanceCoordinator.h"

#include "application/ImportService.h"
#include "application/LibraryQueryService.h"
#include "application/ProjectService.h"
#include "application/SourceChangeMonitor.h"
#include "application/SystemIdleMonitor.h"
#include "application/VideoAnalysisService.h"
#include "domain/Enums.h"
#include "infrastructure/db/GlobalDatabaseManager.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

#include <algorithm>
#include <utility>

BackgroundMaintenanceCoordinator::BackgroundMaintenanceCoordinator(
    ImportService *importService,
    LibraryQueryService *libraryQueryService,
    ProjectService *projectService,
    GlobalDatabaseManager *globalDatabaseManager,
    VideoAnalysisService *videoAnalysisService,
    SourceChangeMonitor *sourceChangeMonitor,
    SystemIdleMonitor *systemIdleMonitor,
    QObject *parent)
    : QObject(parent)
    , m_importService(importService)
    , m_libraryQueryService(libraryQueryService)
    , m_projectService(projectService)
    , m_globalDatabaseManager(globalDatabaseManager)
    , m_videoAnalysisService(videoAnalysisService)
    , m_sourceChangeMonitor(sourceChangeMonitor)
    , m_systemIdleMonitor(systemIdleMonitor)
{
    connect(m_projectService,
            &ProjectService::projectChanged,
            this,
            &BackgroundMaintenanceCoordinator::handleProjectChanged);
    connect(m_importService,
            &ImportService::sourceRootsChanged,
            this,
            [this]() { reloadSourceRoots(false); });
    connect(m_libraryQueryService,
            &LibraryQueryService::sourceRootsChanged,
            this,
            [this]() { reloadSourceRoots(false); });
    connect(m_sourceChangeMonitor,
            &SourceChangeMonitor::sourceChanged,
            this,
            &BackgroundMaintenanceCoordinator::markSourceDirty);
    connect(m_sourceChangeMonitor,
            &SourceChangeMonitor::sourceUnavailable,
            this,
            [this](qint64 sourceRootId, const QString &, const QString &message) {
                m_attemptedThisIdle.insert(sourceRootId);
                setStatusText(message);
            });
    connect(m_systemIdleMonitor, &SystemIdleMonitor::becameIdle, this, [this]() {
        m_attemptedThisIdle.clear();
        m_analysisDispatchBlocked = false;
        setStatusText(QStringLiteral("电脑已空闲 1 小时，开始无感更新索引与自动解析。"));
        processNext();
    });
    connect(m_systemIdleMonitor, &SystemIdleMonitor::activityResumed, this, [this]() {
        setStatusText(QStringLiteral("检测到用户操作，已停止派发新的后台解析任务。"));
    });
    connect(m_importService,
            &ImportService::sourceScanFinished,
            this,
            &BackgroundMaintenanceCoordinator::handleSourceScanFinished);
    connect(m_importService,
            &ImportService::sourceScanFailed,
            this,
            &BackgroundMaintenanceCoordinator::handleSourceScanFailed);
    connect(m_videoAnalysisService,
            &VideoAnalysisService::analysisProgressChanged,
            this,
            [this](const QString &videoKey,
                   qint64,
                   const QString &,
                   int state,
                   const QString &) {
                if (videoKey != m_autoAnalysisVideoKey) {
                    return;
                }
                if (state != static_cast<int>(JobState::Completed)
                    && state != static_cast<int>(JobState::Failed)) {
                    return;
                }
                m_autoAnalysisVideoKey.clear();
                if (m_running && m_systemIdleMonitor->isIdle()) {
                    QTimer::singleShot(0, this, &BackgroundMaintenanceCoordinator::processNext);
                }
            });
    connect(m_videoAnalysisService,
            &VideoAnalysisService::analysisQueueChanged,
            this,
            [this](const QString &, int) {
                if (m_running && m_systemIdleMonitor->isIdle()) {
                    QTimer::singleShot(0, this, &BackgroundMaintenanceCoordinator::processNext);
                }
            });
}

void BackgroundMaintenanceCoordinator::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    handleProjectChanged();
    m_systemIdleMonitor->start(60 * 60 * 1000, 5000);
    emit stateChanged();
}

void BackgroundMaintenanceCoordinator::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_systemIdleMonitor->stop();
    m_sourceChangeMonitor->stop();
    m_sourceRoots.clear();
    m_dirtyGenerations.clear();
    m_attemptedThisIdle.clear();
    m_scanningSourceId = 0;
    m_scanningGeneration = 0;
    m_autoAnalysisVideoKey.clear();
    m_analysisDispatchBlocked = false;
    setStatusText(QString());
    emit stateChanged();
}

bool BackgroundMaintenanceCoordinator::isRunning() const
{
    return m_running;
}

bool BackgroundMaintenanceCoordinator::isSystemIdle() const
{
    return m_systemIdleMonitor && m_systemIdleMonitor->isIdle();
}

int BackgroundMaintenanceCoordinator::pendingSourceCount() const
{
    return m_dirtyGenerations.size();
}

QString BackgroundMaintenanceCoordinator::statusText() const
{
    return m_statusText;
}

void BackgroundMaintenanceCoordinator::handleProjectChanged()
{
    if (!m_running) {
        return;
    }
    m_sourceChangeMonitor->stop();
    m_sourceRoots.clear();
    m_dirtyGenerations.clear();
    m_attemptedThisIdle.clear();
    m_scanningSourceId = 0;
    m_scanningGeneration = 0;
    m_autoAnalysisVideoKey.clear();
    m_analysisDispatchBlocked = false;

    if (!m_projectService->hasOpenProject()) {
        setStatusText(QStringLiteral("后台变化监测等待项目打开。"));
        emit stateChanged();
        return;
    }
    reloadSourceRoots(true);
}

void BackgroundMaintenanceCoordinator::reloadSourceRoots(bool markAllDirty)
{
    if (!m_running || !m_projectService->hasOpenProject()) {
        return;
    }
    const auto roots = m_importService->sourceRoots();
    QHash<qint64, SourceRoot> nextRoots;
    for (const auto &root : roots) {
        nextRoots.insert(root.id, root);
        if (markAllDirty) {
            m_dirtyGenerations.insert(root.id, m_dirtyGenerations.value(root.id, 0) + 1);
        }
    }

    for (auto it = m_dirtyGenerations.begin(); it != m_dirtyGenerations.end();) {
        if (!nextRoots.contains(it.key())) {
            it = m_dirtyGenerations.erase(it);
        } else {
            ++it;
        }
    }
    m_sourceRoots = std::move(nextRoots);
    m_sourceChangeMonitor->setSourceRoots(roots);
    setStatusText(roots.isEmpty()
                      ? QStringLiteral("当前项目还没有可监测的素材源。")
                      : QStringLiteral("正在监测 %1 个素材源的文件变化。").arg(roots.size()));
    emit stateChanged();

    if (m_systemIdleMonitor->isIdle()) {
        QTimer::singleShot(0, this, &BackgroundMaintenanceCoordinator::processNext);
    }
}

void BackgroundMaintenanceCoordinator::markSourceDirty(qint64 sourceRootId,
                                                       const QString &sourcePath)
{
    if (!m_running || !m_sourceRoots.contains(sourceRootId)) {
        return;
    }
    m_dirtyGenerations.insert(sourceRootId,
                              m_dirtyGenerations.value(sourceRootId, 0) + 1);
    // 系统盘即使无人操作也会持续产生系统日志、缓存等变化。一个素材源在同一次
    // 空闲会话中最多扫描一次，扫描期间/之后的新变化保留到下一次空闲会话处理，
    // 避免全盘索引在机器长时间空闲时形成连续重扫循环。
    if (!m_systemIdleMonitor->isIdle()) {
        m_attemptedThisIdle.remove(sourceRootId);
    }
    setStatusText(QStringLiteral("已检测到文件变化：%1；将在电脑空闲 1 小时后自动更新并解析。")
                      .arg(QFileInfo(sourcePath).fileName().isEmpty()
                               ? sourcePath
                               : QFileInfo(sourcePath).fileName()));
    emit stateChanged();
    if (m_systemIdleMonitor->isIdle()) {
        QTimer::singleShot(0, this, &BackgroundMaintenanceCoordinator::processNext);
    }
}

void BackgroundMaintenanceCoordinator::processNext()
{
    if (!m_running
        || !m_projectService->hasOpenProject()
        || !m_systemIdleMonitor->isIdle()
        || m_scanningSourceId > 0) {
        return;
    }

    QList<qint64> dirtyIds = m_dirtyGenerations.keys();
    std::sort(dirtyIds.begin(), dirtyIds.end());
    for (const auto sourceRootId : std::as_const(dirtyIds)) {
        if (m_attemptedThisIdle.contains(sourceRootId) || !m_sourceRoots.contains(sourceRootId)) {
            continue;
        }

        m_attemptedThisIdle.insert(sourceRootId);
        m_scanningSourceId = sourceRootId;
        m_scanningGeneration = m_dirtyGenerations.value(sourceRootId);
        QString errorMessage;
        if (m_importService->rescanSourceRoot(
                sourceRootId,
                QStringLiteral("系统空闲，正在无感更新全量文件索引"),
                &errorMessage)) {
            setStatusText(QStringLiteral("正在无感更新：%1")
                              .arg(m_sourceRoots.value(sourceRootId).name));
            emit stateChanged();
            return;
        }

        m_scanningSourceId = 0;
        m_scanningGeneration = 0;
        setStatusText(errorMessage);
    }

    dispatchNextAnalysis();
}

void BackgroundMaintenanceCoordinator::dispatchNextAnalysis()
{
    if (!m_running
        || !m_systemIdleMonitor->isIdle()
        || m_analysisDispatchBlocked
        || !m_autoAnalysisVideoKey.isEmpty()
        || m_videoAnalysisService->hasPendingAnalysisWork()
        || !m_globalDatabaseManager->isOpen()) {
        return;
    }

    const auto project = m_projectService->currentProject();
    QSqlQuery query(m_globalDatabaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT video_key FROM global_video_asset "
        "WHERE project_uuid = ? AND is_available = 1 AND analysis_status = ? "
        "ORDER BY CASE WHEN asset_type IN (?, ?) THEN 0 ELSE 1 END, updated_at, video_key LIMIT 1"));
    query.addBindValue(project.id);
    query.addBindValue(static_cast<int>(VideoAnalysisStatus::Pending));
    query.addBindValue(static_cast<int>(AssetType::Document));
    query.addBindValue(static_cast<int>(AssetType::Subtitle));
    if (!query.exec()) {
        m_analysisDispatchBlocked = true;
        setStatusText(QStringLiteral("查询待解析素材失败：%1").arg(query.lastError().text()));
        return;
    }
    if (!query.next()) {
        setStatusText(QStringLiteral("后台索引与自动解析已同步。"));
        return;
    }

    const auto videoKey = query.value(0).toString();
    QString errorMessage;
    if (!m_videoAnalysisService->enqueueVideo(videoKey, &errorMessage)) {
        m_analysisDispatchBlocked = true;
        setStatusText(QStringLiteral("自动解析已暂停：%1").arg(errorMessage));
        return;
    }
    m_autoAnalysisVideoKey = videoKey;
    setStatusText(QStringLiteral("正在空闲时自动解析 1 个素材；检测到用户操作后不再派发下一项。"));
    emit stateChanged();
}

void BackgroundMaintenanceCoordinator::handleSourceScanFinished(qint64 sourceRootId)
{
    if (!m_running) {
        return;
    }
    if (sourceRootId == m_scanningSourceId) {
        if (m_dirtyGenerations.value(sourceRootId) == m_scanningGeneration) {
            m_dirtyGenerations.remove(sourceRootId);
        }
        m_scanningSourceId = 0;
        m_scanningGeneration = 0;
        emit stateChanged();
    }
    if (m_systemIdleMonitor->isIdle()) {
        QTimer::singleShot(0, this, &BackgroundMaintenanceCoordinator::processNext);
    }
}

void BackgroundMaintenanceCoordinator::handleSourceScanFailed(qint64 sourceRootId,
                                                              const QString &message)
{
    if (!m_running) {
        return;
    }
    if (sourceRootId == m_scanningSourceId) {
        m_scanningSourceId = 0;
        m_scanningGeneration = 0;
    }
    m_attemptedThisIdle.insert(sourceRootId);
    setStatusText(QStringLiteral("后台索引失败，本轮不再重试：%1").arg(message));
    emit stateChanged();
    if (m_systemIdleMonitor->isIdle()) {
        QTimer::singleShot(0, this, &BackgroundMaintenanceCoordinator::processNext);
    }
}

void BackgroundMaintenanceCoordinator::setStatusText(const QString &statusText)
{
    if (m_statusText == statusText) {
        return;
    }
    m_statusText = statusText;
    emit stateChanged();
}
