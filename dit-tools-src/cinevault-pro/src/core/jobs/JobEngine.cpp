#include "core/jobs/JobEngine.h"

#include "infrastructure/db/DatabaseManager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

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

    if (databaseManager->hasOpenProject()
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
    if (isCurrentProject(projectDatabasePath)) {
        auto *job = findJob(jobId);
        if (!job) {
            return;
        }
        const auto previous = *job;
        job->progress = progress;
        job->detail = detail;
        if (hasProgressContext(progressContext)) {
            job->progressContext = progressContext;
        }
        job->updatedAt = QDateTime::currentDateTime();
        if (!persistJob(*job)) {
            *job = previous;
            return;
        }
        emit jobsChanged();
        return;
    }

    QString persistenceError;
    const auto hasContext = hasProgressContext(progressContext);
    const auto statement = hasContext
        ? QStringLiteral("UPDATE job SET progress = ?, detail = ?, progress_context_json = ?, updated_at = ? WHERE id = ?")
        : QStringLiteral("UPDATE job SET progress = ?, detail = ?, updated_at = ? WHERE id = ?");
    const auto values = hasContext
        ? QVariantList{progress, detail, progressContextJson(progressContext),
                       QDateTime::currentDateTime().toString(Qt::ISODate), jobId}
        : QVariantList{progress, detail,
                       QDateTime::currentDateTime().toString(Qt::ISODate), jobId};
    if (!executeJobUpdate(m_databaseManager,
                          projectDatabasePath,
                          statement,
                          values,
                          &persistenceError)) {
        reportPersistenceError(persistenceError);
    }
}

void JobEngine::updateJobSubject(qint64 jobId, const JobSubject &subject)
{
    updateJobSubjectForProject(currentProjectDatabasePath(), jobId, subject);
}

void JobEngine::updateJobSubjectForProject(const QString &projectDatabasePath,
                                           qint64 jobId,
                                           const JobSubject &subject)
{
    if (isCurrentProject(projectDatabasePath)) {
        if (auto *job = findJob(jobId)) {
            const auto previous = *job;
            job->subject = subject;
            job->updatedAt = QDateTime::currentDateTime();
            if (!persistJob(*job)) {
                *job = previous;
                return;
            }
            emit jobsChanged();
        }
        return;
    }

    QString persistenceError;
    if (!executeJobUpdate(m_databaseManager,
                          projectDatabasePath,
                          QStringLiteral("UPDATE job SET subject_json = ?, updated_at = ? WHERE id = ?"),
                          {subjectJson(subject),
                           QDateTime::currentDateTime().toString(Qt::ISODate),
                           jobId},
                          &persistenceError)) {
        reportPersistenceError(persistenceError);
    }
}

void JobEngine::completeJob(qint64 jobId, const QString &detail)
{
    completeJobForProject(currentProjectDatabasePath(), jobId, detail);
}

void JobEngine::completeJobForProject(const QString &projectDatabasePath, qint64 jobId, const QString &detail)
{
    if (isCurrentProject(projectDatabasePath)) {
        auto *job = findJob(jobId);
        if (!job) {
            return;
        }
        const auto previous = *job;
        job->state = JobState::Completed;
        job->progress = 100;
        job->detail = detail;
        job->errorMessage.clear();
        job->updatedAt = QDateTime::currentDateTime();
        if (!persistJob(*job)) {
            *job = previous;
            return;
        }
        emit jobsChanged();
        return;
    }

    QString persistenceError;
    if (!executeJobUpdate(m_databaseManager,
                          projectDatabasePath,
                          QStringLiteral("UPDATE job SET state = ?, progress = 100, detail = ?, error_message = '', updated_at = ? WHERE id = ?"),
                          {static_cast<int>(JobState::Completed), detail,
                           QDateTime::currentDateTime().toString(Qt::ISODate), jobId},
                          &persistenceError)) {
        reportPersistenceError(persistenceError);
    }
}

void JobEngine::failJob(qint64 jobId, const QString &errorMessage)
{
    failJobForProject(currentProjectDatabasePath(), jobId, errorMessage);
}

void JobEngine::failJobForProject(const QString &projectDatabasePath, qint64 jobId, const QString &errorMessage)
{
    if (isCurrentProject(projectDatabasePath)) {
        auto *job = findJob(jobId);
        if (!job) {
            return;
        }
        const auto previous = *job;
        job->state = JobState::Failed;
        job->errorMessage = errorMessage;
        job->detail = errorMessage;
        job->updatedAt = QDateTime::currentDateTime();
        if (!persistJob(*job)) {
            *job = previous;
            return;
        }
        emit jobsChanged();
        return;
    }

    QString persistenceError;
    if (!executeJobUpdate(m_databaseManager,
                          projectDatabasePath,
                          QStringLiteral("UPDATE job SET state = ?, detail = ?, error_message = ?, updated_at = ? WHERE id = ?"),
                          {static_cast<int>(JobState::Failed), errorMessage, errorMessage,
                           QDateTime::currentDateTime().toString(Qt::ISODate), jobId},
                          &persistenceError)) {
        reportPersistenceError(persistenceError);
    }
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
