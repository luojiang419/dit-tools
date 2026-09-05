#include "core/jobs/JobEngine.h"

#include "application/IndexingWorkCoordinator.h"
#include "infrastructure/db/DatabaseManager.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>

namespace {
constexpr int kMaxLoadedJobs = 500;

bool isFinishedJobState(JobState state)
{
    return state == JobState::Completed
        || state == JobState::Failed
        || state == JobState::Cancelled;
}

bool hasProgressContext(const JobProgressContext &context)
{
    return context.currentStep > 0
        || context.totalSteps > 0
        || !context.stepLabel.trimmed().isEmpty()
        || context.currentItem > 0
        || context.totalItems > 0
        || !context.unitLabel.trimmed().isEmpty()
        || context.currentFrameNumber > 0
        || !context.extraLabel.trimmed().isEmpty();
}

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject jsonObject(const QVariant &value)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(value.toString().toUtf8(), &parseError);
    return parseError.error == QJsonParseError::NoError && document.isObject()
        ? document.object()
        : QJsonObject{};
}

QString subjectJson(const JobSubject &subject)
{
    return compactJson(QJsonObject{
        {QStringLiteral("kind"), subject.kind},
        {QStringLiteral("key"), subject.key},
        {QStringLiteral("name"), subject.name},
        {QStringLiteral("path"), subject.path},
        {QStringLiteral("thumbnailPath"), subject.thumbnailPath},
        {QStringLiteral("thumbnailStatus"), static_cast<int>(subject.thumbnailStatus)},
        {QStringLiteral("typeLabel"), subject.typeLabel}
    });
}

JobSubject subjectFromJson(const QVariant &value)
{
    const auto object = jsonObject(value);
    JobSubject subject;
    subject.kind = object.value(QStringLiteral("kind")).toString();
    subject.key = object.value(QStringLiteral("key")).toString();
    subject.name = object.value(QStringLiteral("name")).toString();
    subject.path = object.value(QStringLiteral("path")).toString();
    subject.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    subject.thumbnailStatus = static_cast<ThumbnailStatus>(
        object.value(QStringLiteral("thumbnailStatus")).toInt(
            static_cast<int>(ThumbnailStatus::Pending)));
    subject.typeLabel = object.value(QStringLiteral("typeLabel")).toString();
    return subject;
}

QString progressContextJson(const JobProgressContext &context)
{
    return compactJson(QJsonObject{
        {QStringLiteral("currentStep"), context.currentStep},
        {QStringLiteral("totalSteps"), context.totalSteps},
        {QStringLiteral("stepLabel"), context.stepLabel},
        {QStringLiteral("currentItem"), context.currentItem},
        {QStringLiteral("totalItems"), context.totalItems},
        {QStringLiteral("unitLabel"), context.unitLabel},
        {QStringLiteral("currentFrameNumber"), context.currentFrameNumber},
        {QStringLiteral("extraLabel"), context.extraLabel}
    });
}

JobProgressContext progressContextFromJson(const QVariant &value)
{
    const auto object = jsonObject(value);
    JobProgressContext context;
    context.currentStep = object.value(QStringLiteral("currentStep")).toInt();
    context.totalSteps = object.value(QStringLiteral("totalSteps")).toInt();
    context.stepLabel = object.value(QStringLiteral("stepLabel")).toString();
    context.currentItem = object.value(QStringLiteral("currentItem")).toVariant().toLongLong();
    context.totalItems = object.value(QStringLiteral("totalItems")).toVariant().toLongLong();
    context.unitLabel = object.value(QStringLiteral("unitLabel")).toString();
    context.currentFrameNumber = object.value(QStringLiteral("currentFrameNumber")).toInt();
    context.extraLabel = object.value(QStringLiteral("extraLabel")).toString();
    return context;
}

QString normalizedDatabasePath(const QString &path)
{
    auto normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

bool executeForProject(DatabaseManager *databaseManager,
                       const QString &projectDatabasePath,
                       const std::function<bool(QSqlDatabase &)> &operation,
                       QString *errorMessage)
{
    if (!databaseManager || projectDatabasePath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("任务所属项目数据库路径为空");
        }
        return false;
    }

    if (QThread::currentThread() == databaseManager->thread()
        && databaseManager->hasOpenProject()
        && normalizedDatabasePath(databaseManager->databaseFilePath())
            == normalizedDatabasePath(projectDatabasePath)) {
        auto db = databaseManager->database();
        if (!db.isOpen()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("当前项目数据库未打开");
            }
            return false;
        }
        return operation(db);
    }

    const auto connectionName = QStringLiteral("job_project_%1")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString openError;
    auto db = databaseManager->openThreadConnectionForPath(projectDatabasePath,
                                                            connectionName,
                                                            &openError);
    if (!db.isOpen()) {
        qWarning() << "Failed to open job project database:" << openError;
        if (errorMessage) {
            *errorMessage = QStringLiteral("打开任务所属项目数据库失败：%1").arg(openError);
        }
        db = QSqlDatabase();
        databaseManager->closeThreadConnection(connectionName);
        return false;
    }

    const auto succeeded = operation(db);
    db.close();
    db = QSqlDatabase();
    databaseManager->closeThreadConnection(connectionName);
    return succeeded;
}

bool executeJobUpdate(DatabaseManager *databaseManager,
                      const QString &projectDatabasePath,
                      const QString &statement,
                      const QVariantList &values,
                      QString *errorMessage)
{
    return executeForProject(databaseManager, projectDatabasePath, [&](QSqlDatabase &db) {
        QSqlQuery query(db);
        if (!query.prepare(statement)) {
            if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
        for (const auto &value : values) {
            query.addBindValue(value);
        }
        if (!query.exec()) {
            qWarning() << "Failed to persist background job state:" << query.lastError().text();
            if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
        return true;
    }, errorMessage);
}
}

JobEngine::JobEngine(DatabaseManager *databaseManager, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
{
    m_persistenceTimer.setSingleShot(true);
    m_persistenceTimer.setInterval(200);
    m_persistencePool.setMaxThreadCount(1);
    m_persistencePool.setExpiryTimeout(-1);
    connect(&m_persistenceTimer,
            &QTimer::timeout,
            this,
            &JobEngine::flushPendingPersistence);
}

void JobEngine::setWorkCoordinator(IndexingWorkCoordinator *workCoordinator)
{
    m_workCoordinator = workCoordinator;
}

qint64 JobEngine::createJob(JobType type,
                            const QString &title,
                            const QString &detail,
                            qint64 sourceRootId,
                            const JobSubject &subject,
                            const JobProgressContext &progressContext)
{
    return appendJob(type, JobState::Running, title, detail, sourceRootId, subject, progressContext);
}

qint64 JobEngine::queueJob(JobType type,
                           const QString &title,
                           const QString &detail,
                           qint64 sourceRootId,
                           const JobSubject &subject,
                           const JobProgressContext &progressContext)
{
    return appendJob(type, JobState::Pending, title, detail, sourceRootId, subject, progressContext);
}

qint64 JobEngine::appendJob(JobType type,
                            JobState state,
                            const QString &title,
                            const QString &detail,
                            qint64 sourceRootId,
                            const JobSubject &subject,
                            const JobProgressContext &progressContext)
{
    Job job;
    job.id = m_nextId++;
    job.type = type;
    job.state = state;
    job.title = title;
    job.detail = detail;
    job.progress = state == JobState::Pending ? 0 : 0;
    job.sourceRootId = sourceRootId;
    job.subject = subject;
    job.progressContext = progressContext;
    job.startedAt = QDateTime::currentDateTime();
    job.updatedAt = job.startedAt;
    if (!persistJob(job)) {
        return 0;
    }
    m_jobs.prepend(job);
    emit jobsChanged();
    return job.id;
}

void JobEngine::updateJob(qint64 jobId, qint64 progress, const QString &detail, const JobProgressContext &progressContext)
{
    updateJobForProject(currentProjectDatabasePath(), jobId, progress, detail, progressContext);
}

void JobEngine::updateJobForProject(const QString &projectDatabasePath,
                                    qint64 jobId,
                                    qint64 progress,
                                    const QString &detail,
                                    const JobProgressContext &progressContext)
{
    const auto hasContext = hasProgressContext(progressContext);
    const auto updatedAt = QDateTime::currentDateTime();
    if (isCurrentProject(projectDatabasePath)) {
        auto *job = findJob(jobId);
        if (!job) {
            return;
        }
        job->progress = progress;
        job->detail = detail;
        if (hasContext) {
            job->progressContext = progressContext;
        }
        job->updatedAt = updatedAt;
        emit jobsChanged();
    }

    const auto statement = hasContext
        ? QStringLiteral("UPDATE job SET progress = ?, detail = ?, progress_context_json = ?, updated_at = ? WHERE id = ?")
        : QStringLiteral("UPDATE job SET progress = ?, detail = ?, updated_at = ? WHERE id = ?");
    const auto values = hasContext
        ? QVariantList{progress, detail, progressContextJson(progressContext),
                       updatedAt.toString(Qt::ISODate), jobId}
        : QVariantList{progress, detail,
                       updatedAt.toString(Qt::ISODate), jobId};
    enqueuePersistence(QStringLiteral("%1|%2|progress")
                           .arg(normalizedDatabasePath(projectDatabasePath))
                           .arg(jobId),
                       projectDatabasePath,
                       statement,
                       values);
}

void JobEngine::updateJobSubject(qint64 jobId, const JobSubject &subject)
{
    updateJobSubjectForProject(currentProjectDatabasePath(), jobId, subject);
}

void JobEngine::updateJobSubjectForProject(const QString &projectDatabasePath,
                                           qint64 jobId,
                                           const JobSubject &subject)
{
    const auto updatedAt = QDateTime::currentDateTime();
    if (isCurrentProject(projectDatabasePath)) {
        if (auto *job = findJob(jobId)) {
            job->subject = subject;
            job->updatedAt = updatedAt;
            emit jobsChanged();
        }
    }

    enqueuePersistence(QStringLiteral("%1|%2|subject")
                           .arg(normalizedDatabasePath(projectDatabasePath))
                           .arg(jobId),
                       projectDatabasePath,
                       QStringLiteral("UPDATE job SET subject_json = ?, updated_at = ? WHERE id = ?"),
                       {subjectJson(subject), updatedAt.toString(Qt::ISODate), jobId});
}

void JobEngine::completeJob(qint64 jobId, const QString &detail)
{
    completeJobForProject(currentProjectDatabasePath(), jobId, detail);
}

void JobEngine::completeJobForProject(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    const auto updatedAt = QDateTime::currentDateTime();
    if (isCurrentProject(projectDatabasePath)) {
        auto *job = findJob(jobId);
        if (!job) {
            return;
        }
        job->state = JobState::Completed;
        job->progress = 100;
        job->detail = detail;
        job->errorMessage.clear();
        job->updatedAt = updatedAt;
        emit jobsChanged();
    }

    const auto keyPrefix = QStringLiteral("%1|%2|")
                               .arg(normalizedDatabasePath(projectDatabasePath))
                               .arg(jobId);
    m_pendingPersistence.remove(keyPrefix + QStringLiteral("progress"));
    enqueuePersistence(keyPrefix + QStringLiteral("terminal"),
                       projectDatabasePath,
                       QStringLiteral("UPDATE job SET state = ?, progress = 100, detail = ?, error_message = '', updated_at = ? WHERE id = ?"),
                       {static_cast<int>(JobState::Completed), detail,
                        updatedAt.toString(Qt::ISODate), jobId},
                       true);
}

void JobEngine::failJob(qint64 jobId, const QString &errorMessage)
{
    failJobForProject(currentProjectDatabasePath(), jobId, errorMessage);
}

void JobEngine::failJobForProject(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage)
{
    const auto updatedAt = QDateTime::currentDateTime();
    if (isCurrentProject(projectDatabasePath)) {
        auto *job = findJob(jobId);
        if (!job) {
            return;
        }
        job->state = JobState::Failed;
        job->errorMessage = errorMessage;
        job->detail = errorMessage;
        job->updatedAt = updatedAt;
        emit jobsChanged();
    }

    const auto keyPrefix = QStringLiteral("%1|%2|")
                               .arg(normalizedDatabasePath(projectDatabasePath))
                               .arg(jobId);
    m_pendingPersistence.remove(keyPrefix + QStringLiteral("progress"));
    enqueuePersistence(keyPrefix + QStringLiteral("terminal"),
                       projectDatabasePath,
                       QStringLiteral("UPDATE job SET state = ?, detail = ?, error_message = ?, updated_at = ? WHERE id = ?"),
                       {static_cast<int>(JobState::Failed), errorMessage, errorMessage,
                        updatedAt.toString(Qt::ISODate), jobId},
                       true);
}

QString JobEngine::currentProjectDatabasePath() const
{
    return m_databaseManager && m_databaseManager->hasOpenProject()
        ? m_databaseManager->databaseFilePath()
        : QString();
}

bool JobEngine::isCurrentProject(const QString &projectDatabasePath) const
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        return projectDatabasePath.trimmed().isEmpty();
    }
    return !projectDatabasePath.trimmed().isEmpty()
        && normalizedDatabasePath(m_databaseManager->databaseFilePath())
            == normalizedDatabasePath(projectDatabasePath);
}

void JobEngine::reloadJobs()
{
    m_jobs.clear();
    m_nextId = 1;

    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        emit jobsChanged();
        return;
    }

    const auto interruptedMessage = QStringLiteral("应用上次退出时任务已中断；可恢复任务将自动重新开始");
    const auto now = QDateTime::currentDateTime();
    QSqlQuery interruptQuery(m_databaseManager->database());
    interruptQuery.prepare(QStringLiteral(
        "UPDATE job SET state = ?, detail = ?, error_message = ?, updated_at = ? "
        "WHERE state IN (?, ?)"));
    interruptQuery.addBindValue(static_cast<int>(JobState::Failed));
    interruptQuery.addBindValue(interruptedMessage);
    interruptQuery.addBindValue(interruptedMessage);
    interruptQuery.addBindValue(now.toString(Qt::ISODate));
    interruptQuery.addBindValue(static_cast<int>(JobState::Pending));
    interruptQuery.addBindValue(static_cast<int>(JobState::Running));
    if (!interruptQuery.exec()) {
        reportPersistenceError(
            QStringLiteral("恢复任务历史时标记中断任务失败：%1")
                .arg(interruptQuery.lastError().text()));
        emit jobsChanged();
        return;
    }

    QSqlQuery query(m_databaseManager->database());
    if (!query.exec(QStringLiteral(
            "SELECT j.id, j.type, j.state, j.title, COALESCE(j.detail, ''), "
            "COALESCE(j.error_message, ''), j.progress, j.source_root_id, "
            "COALESCE(j.subject_json, '{}'), COALESCE(j.progress_context_json, '{}'), "
            "j.started_at, j.updated_at, COALESCE(sr.name, ''), COALESCE(sr.path, '') "
            "FROM job j LEFT JOIN source_root sr ON sr.id = j.source_root_id "
            "ORDER BY j.id DESC LIMIT %1").arg(kMaxLoadedJobs))) {
        reportPersistenceError(
            QStringLiteral("读取任务历史失败：%1").arg(query.lastError().text()));
        emit jobsChanged();
        return;
    }

    while (query.next()) {
        Job job;
        job.id = query.value(0).toLongLong();
        job.type = static_cast<JobType>(query.value(1).toInt());
        job.state = static_cast<JobState>(query.value(2).toInt());
        job.title = query.value(3).toString();
        job.detail = query.value(4).toString();
        job.errorMessage = query.value(5).toString();
        job.progress = query.value(6).toLongLong();
        job.sourceRootId = query.value(7).toLongLong();
        job.subject = subjectFromJson(query.value(8));
        job.progressContext = progressContextFromJson(query.value(9));
        job.startedAt = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
        job.updatedAt = QDateTime::fromString(query.value(11).toString(), Qt::ISODate);
        if (job.subject.kind.trimmed().isEmpty() && job.sourceRootId > 0) {
            job.subject.kind = QStringLiteral("sourceRoot");
            job.subject.key = QString::number(job.sourceRootId);
            job.subject.name = query.value(12).toString();
            job.subject.path = query.value(13).toString();
            job.subject.typeLabel = QStringLiteral("素材源");
        }
        m_jobs.append(job);
        m_nextId = qMax(m_nextId, job.id + 1);
    }

    emit jobsChanged();
}

void JobEngine::clearJobs()
{
    if (m_jobs.isEmpty()) {
        return;
    }
    m_jobs.clear();
    emit jobsChanged();
}

bool JobEngine::removeFinishedJob(qint64 jobId)
{
    qsizetype jobIndex = -1;
    for (qsizetype index = 0; index < m_jobs.size(); ++index) {
        if (m_jobs.at(index).id == jobId) {
            jobIndex = index;
            break;
        }
    }
    if (jobIndex < 0 || !isFinishedJobState(m_jobs.at(jobIndex).state)) {
        return false;
    }

    if (m_databaseManager && m_databaseManager->hasOpenProject()) {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("DELETE FROM job WHERE id = ?"));
        query.addBindValue(jobId);
        if (!query.exec()) {
            reportPersistenceError(
                QStringLiteral("删除已结束任务失败：%1").arg(query.lastError().text()));
            return false;
        }
    }

    m_jobs.removeAt(jobIndex);
    emit jobsChanged();
    return true;
}

void JobEngine::clearFinishedJobs()
{
    QVector<Job> keptJobs;
    keptJobs.reserve(m_jobs.size());
    for (const auto &job : m_jobs) {
        if (!isFinishedJobState(job.state)) {
            keptJobs.append(job);
        }
    }

    if (keptJobs.size() == m_jobs.size()) {
        return;
    }

    if (m_databaseManager && m_databaseManager->hasOpenProject()) {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral("DELETE FROM job WHERE state IN (?, ?, ?)"));
        query.addBindValue(static_cast<int>(JobState::Completed));
        query.addBindValue(static_cast<int>(JobState::Failed));
        query.addBindValue(static_cast<int>(JobState::Cancelled));
        if (!query.exec()) {
            reportPersistenceError(
                QStringLiteral("清理已结束任务失败：%1").arg(query.lastError().text()));
            return;
        }
    }
    m_jobs = keptJobs;
    emit jobsChanged();
}

void JobEngine::clearFailedJobsForRetry(qint64 sourceRootId,
                                        const QVector<JobType> &types)
{
    if (sourceRootId <= 0 || types.isEmpty()) {
        return;
    }

    QSet<int> typeValues;
    QStringList placeholders;
    for (const auto type : types) {
        const auto value = static_cast<int>(type);
        if (typeValues.contains(value)) {
            continue;
        }
        typeValues.insert(value);
        placeholders.append(QStringLiteral("?"));
    }
    if (typeValues.isEmpty()) {
        return;
    }

    if (m_databaseManager && m_databaseManager->hasOpenProject()) {
        QSqlQuery query(m_databaseManager->database());
        query.prepare(QStringLiteral(
            "DELETE FROM job WHERE state = ? AND source_root_id = ? AND type IN (%1)")
                          .arg(placeholders.join(QStringLiteral(","))));
        query.addBindValue(static_cast<int>(JobState::Failed));
        query.addBindValue(sourceRootId);
        for (const auto value : std::as_const(typeValues)) {
            query.addBindValue(value);
        }
        if (!query.exec()) {
            reportPersistenceError(
                QStringLiteral("自动清理已替代失败任务失败：%1")
                    .arg(query.lastError().text()));
            return;
        }
    }

    QVector<Job> keptJobs;
    keptJobs.reserve(m_jobs.size());
    for (const auto &job : std::as_const(m_jobs)) {
        if (job.state == JobState::Failed
            && job.sourceRootId == sourceRootId
            && typeValues.contains(static_cast<int>(job.type))) {
            continue;
        }
        keptJobs.append(job);
    }
    if (keptJobs.size() != m_jobs.size()) {
        m_jobs = std::move(keptJobs);
        emit jobsChanged();
    }
}

QVector<Job> JobEngine::jobs() const
{
    return m_jobs;
}

void JobEngine::enqueuePersistence(const QString &key,
                                   const QString &projectDatabasePath,
                                   const QString &statement,
                                   const QVariantList &values,
                                   bool flushImmediately)
{
    if (!m_databaseManager || projectDatabasePath.trimmed().isEmpty()) {
        return;
    }
    PendingPersistence pending;
    pending.key = key;
    pending.projectDatabasePath = projectDatabasePath;
    pending.statement = statement;
    pending.values = values;
    m_pendingPersistence.insert(key, std::move(pending));
    if (flushImmediately && !m_persistenceRunning) {
        m_persistenceTimer.stop();
        flushPendingPersistence();
    } else {
        m_persistenceTimer.start();
    }
}

void JobEngine::flushPendingPersistence()
{
    if (m_persistenceRunning || m_pendingPersistence.isEmpty()) {
        return;
    }
    QVector<PendingPersistence> batch;
    batch.reserve(m_pendingPersistence.size());
    for (auto &pending : m_pendingPersistence) {
        batch.append(std::move(pending));
    }
    m_pendingPersistence.clear();
    startPersistenceBatch(std::move(batch), true);
}

void JobEngine::startPersistenceBatch(QVector<PendingPersistence> batch,
                                      bool observeCompletion)
{
    if (batch.isEmpty()) {
        return;
    }
    if (observeCompletion) {
        m_persistenceRunning = true;
    }
    m_persistenceFuture = QtConcurrent::run(&m_persistencePool,
                                            [this, batch = std::move(batch)]() mutable {
                                                return executePersistenceBatch(std::move(batch));
                                            });
    if (!observeCompletion) {
        return;
    }
    auto *watcher = new QFutureWatcher<PersistenceBatchResult>(this);
    connect(watcher, &QFutureWatcher<PersistenceBatchResult>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!m_persistenceRunning) {
            return;
        }
        m_persistenceRunning = false;
        handlePersistenceResult(result);
    });
    watcher->setFuture(m_persistenceFuture);
}

void JobEngine::waitForPersistence()
{
    m_persistenceTimer.stop();
    while (true) {
        m_persistencePool.waitForDone();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (m_persistenceRunning) {
            m_persistenceFuture.waitForFinished();
            const auto result = m_persistenceFuture.result();
            m_persistenceRunning = false;
            handlePersistenceResult(result);
            m_persistenceFuture = {};
            continue;
        }
        if (m_pendingPersistence.isEmpty()) {
            break;
        }

        QVector<PendingPersistence> batch;
        batch.reserve(m_pendingPersistence.size());
        for (auto &pending : m_pendingPersistence) {
            batch.append(std::move(pending));
        }
        m_pendingPersistence.clear();
        m_persistenceRunning = true;
        auto future = QtConcurrent::run(&m_persistencePool,
                                        [this, batch = std::move(batch)]() mutable {
                                            return executePersistenceBatch(std::move(batch));
                                        });
        future.waitForFinished();
        const auto result = future.result();
        m_persistenceRunning = false;
        handlePersistenceResult(result);
    }
}

JobEngine::PersistenceBatchResult JobEngine::executePersistenceBatch(
    QVector<PendingPersistence> batch) const
{
    PersistenceBatchResult result;
    for (auto &pending : batch) {
        QString errorMessage;
        auto writerLease = m_workCoordinator
            ? m_workCoordinator->acquire({IndexingWorkCoordinator::Resource::SqliteWriter,
                                          IndexingWorkCoordinator::Priority::Background,
                                          false,
                                          m_workCoordinator->currentGeneration()})
            : IndexingWorkCoordinator::Lease{};
        const auto persisted = (!m_workCoordinator || writerLease)
            && executeJobUpdate(m_databaseManager,
                                pending.projectDatabasePath,
                                pending.statement,
                                pending.values,
                                &errorMessage);
        if (!persisted) {
            if (errorMessage.isEmpty()) {
                errorMessage = QStringLiteral("任务写入等待 SqliteWriter 资源失败");
            }
            ++pending.attempt;
            result.failed.append(std::move(pending));
            if (result.errorMessage.isEmpty()) {
                result.errorMessage = errorMessage;
            }
        }
    }
    return result;
}

void JobEngine::handlePersistenceResult(const PersistenceBatchResult &result)
{
    for (const auto &failed : result.failed) {
        if (failed.attempt < 3 && !m_pendingPersistence.contains(failed.key)) {
            m_pendingPersistence.insert(failed.key, failed);
        }
    }
    if (!result.errorMessage.isEmpty()) {
        reportPersistenceError(result.errorMessage);
    }
    if (!m_pendingPersistence.isEmpty() && !m_persistenceRunning) {
        m_persistenceTimer.start(result.failed.isEmpty() ? 0 : 500);
    }
}

bool JobEngine::persistJob(const Job &job)
{
    if (!m_databaseManager || !m_databaseManager->hasOpenProject()) {
        return true;
    }

    QSqlQuery query(m_databaseManager->database());
    if (!query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO job (id, type, state, title, detail, error_message, progress, source_root_id, "
            "subject_json, progress_context_json, started_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"))) {
        reportPersistenceError(
            QStringLiteral("准备保存任务失败：%1").arg(query.lastError().text()));
        return false;
    }
    query.addBindValue(job.id);
    query.addBindValue(static_cast<int>(job.type));
    query.addBindValue(static_cast<int>(job.state));
    query.addBindValue(job.title);
    query.addBindValue(job.detail);
    query.addBindValue(job.errorMessage);
    query.addBindValue(job.progress);
    query.addBindValue(job.sourceRootId);
    query.addBindValue(subjectJson(job.subject));
    query.addBindValue(progressContextJson(job.progressContext));
    query.addBindValue(job.startedAt.toString(Qt::ISODate));
    query.addBindValue(job.updatedAt.toString(Qt::ISODate));
    if (!query.exec()) {
        reportPersistenceError(
            QStringLiteral("保存任务失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

void JobEngine::reportPersistenceError(const QString &errorMessage)
{
    const auto message = errorMessage.trimmed().isEmpty()
        ? QStringLiteral("任务历史数据库操作失败")
        : errorMessage.trimmed();
    qWarning().noquote() << message;
    emit persistenceError(message);
}

Job *JobEngine::findJob(qint64 jobId)
{
    for (auto &job : m_jobs) {
        if (job.id == jobId) {
            return &job;
        }
    }
    return nullptr;
}
