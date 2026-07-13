#include "application/MaterialBackupService.h"

#include "core/backup/BackupCopyEngine.h"
#include "core/backup/BackupPlanner.h"
#include "core/backup/VolumeEjectService.h"
#include "core/jobs/JobEngine.h"

#include <QtConcurrent>

#include <QMetaObject>

namespace {
JobSubject backupSubject(const BackupPlan &plan)
{
    JobSubject subject;
    subject.kind = QStringLiteral("backup");
    subject.key = plan.batchName;
    subject.name = plan.batchName;
    subject.path = plan.sources.isEmpty() ? QString() : plan.sources.first().path;
    subject.typeLabel = QStringLiteral("备份批次");
    return subject;
}

JobProgressContext backupProgressContext(const BackupDestinationTask &task, BackupVerificationMode verificationMode)
{
    JobProgressContext context;
    context.currentStep = task.state == BackupTaskState::Verifying ? 2 : 1;
    context.totalSteps = verificationMode == BackupVerificationMode::Off ? 1 : 2;
    context.stepLabel = task.state == BackupTaskState::Verifying ? QStringLiteral("校验文件") : QStringLiteral("复制文件");
    context.currentItem = task.copiedBytes;
    context.totalItems = task.totalBytes;
    context.unitLabel = QStringLiteral("字节");
    if (task.totalFiles > 0) {
        context.extraLabel = QStringLiteral("%1/%2个文件").arg(task.copiedFiles).arg(task.totalFiles);
    }
    return context;
}
}

MaterialBackupService::MaterialBackupService(JobEngine *jobEngine, QObject *parent)
    : QObject(parent)
    , m_jobEngine(jobEngine)
    , m_copyEngine(new BackupCopyEngine(this))
{
}

BackupPlan MaterialBackupService::buildPlan(const BackupRequest &request) const
{
    return BackupPlanner().buildPlan(request);
}

bool MaterialBackupService::isRunning() const
{
    return m_running;
}

bool MaterialBackupService::startBackup(const BackupPlan &plan)
{
    if (m_running || !plan.valid) {
        return false;
    }

    m_running = true;
    emit backupStarted();
    emit messageChanged(QStringLiteral("素材备份已开始。"));

    const auto jobId = m_jobEngine
        ? m_jobEngine->createJob(JobType::Backup,
                                 QStringLiteral("素材备份 %1").arg(plan.batchName),
                                 QStringLiteral("准备复制 %1 个文件").arg(plan.totalFiles),
                                 0,
                                 backupSubject(plan),
                                 JobProgressContext{1, plan.verificationMode == BackupVerificationMode::Off ? 1 : 2, QStringLiteral("复制文件"), 0, plan.totalBytes, QStringLiteral("字节"), 0, QStringLiteral("0/%1个文件").arg(plan.totalFiles)})
        : 0;

    auto future = QtConcurrent::run([this, plan, jobId]() {
        VolumeEjectService ejectService;
        auto progress = [this, jobId, verificationMode = plan.verificationMode](const BackupDestinationTask &task) {
            QMetaObject::invokeMethod(this, [this, jobId, task, verificationMode]() {
                emit backupTaskUpdated(task);
                if (m_jobEngine && jobId > 0) {
                    const auto progressValue = task.totalBytes > 0
                        ? qBound<qint64>(qint64{0}, (task.copiedBytes * qint64{100}) / task.totalBytes, qint64{99})
                        : qint64{0};
                    m_jobEngine->updateJob(jobId, progressValue, task.statusText, backupProgressContext(task, verificationMode));
                }
            }, Qt::QueuedConnection);
        };

        const auto result = plan.cascadeEnabled
            ? m_copyEngine->copyCascade(plan, &ejectService, progress)
            : m_copyEngine->copyDirect(plan, progress);

        QMetaObject::invokeMethod(this, [this, jobId, result]() {
            m_running = false;
            if (m_jobEngine && jobId > 0) {
                if (result.cancelled) {
                    m_jobEngine->failJob(jobId, QStringLiteral("素材备份已取消"));
                } else if (!result.errors.isEmpty()) {
                    m_jobEngine->failJob(jobId, result.errors.join(QStringLiteral("\n")));
                } else {
                    m_jobEngine->completeJob(jobId, QStringLiteral("素材备份完成"));
                }
            }
            emit messageChanged(result.success
                                    ? QStringLiteral("素材备份完成。")
                                    : QStringLiteral("素材备份结束，存在失败或取消。"));
            emit backupFinished(result);
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
    return true;
}

void MaterialBackupService::cancelBackup()
{
    if (m_copyEngine) {
        m_copyEngine->requestCancel();
    }
    emit messageChanged(QStringLiteral("正在请求取消素材备份。"));
}
