#include "application/UpdaterSession.h"

#include "application/UpdateService.h"
#include "shared/Paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace {
constexpr auto kSessionArg = "--run-update-session=";
constexpr auto kVersionArg = "--update-version=";
constexpr auto kInstallerArg = "--update-installer=";
constexpr auto kInstallRootArg = "--update-install-root=";
constexpr auto kExecutableNameArg = "--update-executable-name=";
constexpr auto kOldPidArg = "--update-old-pid=";
constexpr qint64 kOldProcessWaitTimeoutMs = 120000;
constexpr int kInstallProgressStart = 10;
constexpr int kInstallProgressEnd = 90;

QString sessionRoot(const QString &sessionId)
{
    return QDir(Paths::updatesRoot())
        .filePath(QStringLiteral("windows/sessions/%1").arg(sessionId));
}
}

UpdaterSessionRunner::UpdaterSessionRunner(QObject *parent)
    : QObject(parent)
{
}

QStringList UpdaterSessionRunner::buildArguments(const UpdaterInstallSession &session)
{
    return {
        QString::fromLatin1(kSessionArg) + session.sessionId,
        QString::fromLatin1(kVersionArg) + session.versionTag,
        QString::fromLatin1(kInstallerArg) + session.installerPath,
        QString::fromLatin1(kInstallRootArg) + session.installRoot,
        QString::fromLatin1(kExecutableNameArg) + session.executableName,
        QString::fromLatin1(kOldPidArg) + QString::number(session.oldProcessId)
    };
}

bool UpdaterSessionRunner::parseArguments(const QStringList &arguments,
                                          UpdaterInstallSession *session,
                                          QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const auto sessionId = safeSessionId(argumentValue(arguments, QString::fromLatin1(kSessionArg)));
    if (sessionId.isEmpty()) {
        return false;
    }

    UpdaterInstallSession parsed;
    parsed.sessionId = sessionId;
    parsed.versionTag = UpdateService::normalizeVersionTag(
        argumentValue(arguments, QString::fromLatin1(kVersionArg)));
    parsed.installerPath = argumentValue(arguments, QString::fromLatin1(kInstallerArg)).trimmed();
    parsed.installRoot = argumentValue(arguments, QString::fromLatin1(kInstallRootArg)).trimmed();
    parsed.executableName = QFileInfo(
        argumentValue(arguments, QString::fromLatin1(kExecutableNameArg))).fileName();
    bool pidOk = false;
    parsed.oldProcessId = argumentValue(arguments, QString::fromLatin1(kOldPidArg)).toLongLong(&pidOk);

    if (parsed.versionTag.isEmpty()
        || parsed.installerPath.isEmpty()
        || parsed.installRoot.isEmpty()
        || parsed.executableName.isEmpty()
        || !pidOk
        || parsed.oldProcessId <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新会话参数不完整。");
        }
        return false;
    }

    if (session) {
        *session = parsed;
    }
    return true;
}

bool UpdaterSessionRunner::launchDetached(const QString &versionTag,
                                          const QString &installerPath,
                                          const QString &installRoot,
                                          const QString &sourceExecutablePath,
                                          qint64 oldProcessId,
                                          QString *errorMessage)
{
#if !defined(Q_OS_WIN)
    Q_UNUSED(versionTag)
    Q_UNUSED(installerPath)
    Q_UNUSED(installRoot)
    Q_UNUSED(sourceExecutablePath)
    Q_UNUSED(oldProcessId)
    if (errorMessage) {
        *errorMessage = QStringLiteral("当前平台暂未实现独立自动更新。");
    }
    return false;
#else
    if (errorMessage) {
        errorMessage->clear();
    }

    const QFileInfo installerInfo(installerPath);
    if (!installerInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新安装包不存在：%1").arg(installerPath);
        }
        return false;
    }

    const auto normalizedVersion = UpdateService::normalizeVersionTag(versionTag);
    if (normalizedVersion.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新版本号无效：%1").arg(versionTag);
        }
        return false;
    }

    UpdaterInstallSession session;
    session.sessionId = safeSessionId(
        QStringLiteral("update_%1").arg(QDateTime::currentMSecsSinceEpoch()));
    session.versionTag = normalizedVersion;
    session.installerPath = installerInfo.absoluteFilePath();
    session.installRoot = QDir(installRoot).absolutePath();
    session.executableName = QFileInfo(sourceExecutablePath).fileName();
    session.oldProcessId = oldProcessId;

    QString stagedExecutablePath;
    if (!stageRuntime(session.sessionId,
                      QFileInfo(sourceExecutablePath).absolutePath(),
                      sourceExecutablePath,
                      &stagedExecutablePath,
                      errorMessage)) {
        return false;
    }

    qint64 updaterPid = 0;
    if (!QProcess::startDetached(stagedExecutablePath,
                                 buildArguments(session),
                                 QFileInfo(stagedExecutablePath).absolutePath(),
                                 &updaterPid)
        || updaterPid <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法启动独立更新进度窗口。");
        }
        return false;
    }
    return true;
#endif
}

int UpdaterSessionRunner::parseInstallerProgress(const QByteArray &data)
{
    bool ok = false;
    const int progress = QString::fromLatin1(data).trimmed().toInt(&ok);
    return ok && progress >= 0 && progress <= 100 ? progress : -1;
}

int UpdaterSessionRunner::overallProgressForInstallerProgress(int installerProgress)
{
    const int boundedProgress = qBound(0, installerProgress, 100);
    return kInstallProgressStart
        + ((kInstallProgressEnd - kInstallProgressStart) * boundedProgress + 50) / 100;
}

void UpdaterSessionRunner::start(const UpdaterInstallSession &session)
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_session = session;

    if (!QFileInfo::exists(m_session.installerPath)) {
        completeFailure(QStringLiteral("更新安装包不存在：%1").arg(m_session.installerPath));
        return;
    }
    if (!QFileInfo(m_session.installRoot).isDir()) {
        completeFailure(QStringLiteral("安装目录不存在：%1").arg(m_session.installRoot));
        return;
    }

    emitProgress(0,
                 2,
                 QStringLiteral("准备安装"),
                 QStringLiteral("正在准备独立更新器会话..."));
    QTimer::singleShot(300, this, &UpdaterSessionRunner::waitForOldProcess);
}

QString UpdaterSessionRunner::argumentValue(const QStringList &arguments, const QString &prefix)
{
    for (const auto &argument : arguments) {
        if (argument.startsWith(prefix, Qt::CaseInsensitive)) {
            return argument.mid(prefix.size());
        }
    }
    return {};
}

QString UpdaterSessionRunner::safeSessionId(const QString &value)
{
    auto safe = value.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return safe.left(96);
}

bool UpdaterSessionRunner::stageRuntime(const QString &sessionId,
                                        const QString &sourceRoot,
                                        const QString &sourceExecutablePath,
                                        QString *stagedExecutablePath,
                                        QString *errorMessage)
{
    const auto runtimeRoot = QDir(Paths::updatesRoot())
                                 .filePath(QStringLiteral("windows/staging/%1_runtime")
                                               .arg(safeSessionId(sessionId)));
    QDir runtimeDir(runtimeRoot);
    if (runtimeDir.exists() && !runtimeDir.removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法清理旧更新器运行时：%1").arg(runtimeRoot);
        }
        return false;
    }
    if (!QDir().mkpath(runtimeRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建更新器运行时目录：%1").arg(runtimeRoot);
        }
        return false;
    }

    if (!copyRuntimeDirectory(sourceRoot, runtimeRoot, errorMessage)) {
        return false;
    }

    const auto executablePath = QDir(runtimeRoot).filePath(QFileInfo(sourceExecutablePath).fileName());
    if (!QFileInfo::exists(executablePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("临时更新器主程序不存在：%1").arg(executablePath);
        }
        return false;
    }
    if (stagedExecutablePath) {
        *stagedExecutablePath = executablePath;
    }
    return true;
}

bool UpdaterSessionRunner::copyRuntimeDirectory(const QString &sourceRoot,
                                                const QString &targetRoot,
                                                QString *errorMessage)
{
    const QDir sourceDir(sourceRoot);
    if (!sourceDir.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新器源目录不存在：%1").arg(sourceRoot);
        }
        return false;
    }

    const auto entries = sourceDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries,
        QDir::DirsFirst | QDir::Name);
    for (const auto &entry : entries) {
        const auto name = entry.fileName();
        if (entry.isDir()
            && (name.compare(QStringLiteral("data"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("ffmpeg"), Qt::CaseInsensitive) == 0)) {
            continue;
        }

        const auto targetPath = QDir(targetRoot).filePath(name);
        if (entry.isDir()) {
            if (!QDir().mkpath(targetPath)
                || !copyRuntimeDirectory(entry.absoluteFilePath(), targetPath, errorMessage)) {
                return false;
            }
            continue;
        }
        if (!entry.isFile()) {
            continue;
        }
        QFile::remove(targetPath);
        if (!QFile::copy(entry.absoluteFilePath(), targetPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法复制更新器运行时文件：%1").arg(entry.absoluteFilePath());
            }
            return false;
        }
    }
    return true;
}

bool UpdaterSessionRunner::processExists(qint64 processId)
{
#if defined(Q_OS_WIN)
    if (processId <= 0 || processId == QCoreApplication::applicationPid()) {
        return false;
    }
    const auto handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                    FALSE,
                                    static_cast<DWORD>(processId));
    if (!handle) {
        return false;
    }
    const auto status = WaitForSingleObject(handle, 0);
    CloseHandle(handle);
    return status == WAIT_TIMEOUT;
#else
    Q_UNUSED(processId)
    return false;
#endif
}

QString UpdaterSessionRunner::powerShellLiteral(const QString &value)
{
    auto escaped = QDir::toNativeSeparators(value);
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

void UpdaterSessionRunner::emitProgress(int stepIndex,
                                        int percentage,
                                        const QString &stepLabel,
                                        const QString &message,
                                        const QString &substep,
                                        bool isError,
                                        bool isSuccess)
{
    UpdaterProgressEvent event;
    event.stepIndex = stepIndex;
    m_lastPercentage = qBound(m_lastPercentage, percentage, 100);
    event.percentage = m_lastPercentage;
    event.stepLabel = stepLabel;
    event.message = message;
    event.substep = substep;
    event.isError = isError;
    event.isSuccess = isSuccess;
    emit progressChanged(event);
}

void UpdaterSessionRunner::waitForOldProcess()
{
    emitProgress(1,
                 5,
                 QStringLiteral("关闭旧版本"),
                 QStringLiteral("正在等待旧版本退出..."),
                 QStringLiteral("正在确认主程序 PID %1 的退出状态，更新窗口会继续运行。")
                     .arg(m_session.oldProcessId));
    m_waitStartedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_lastWaitDetailSecond = -1;

    m_waitTimer = new QTimer(this);
    m_waitTimer->setInterval(500);
    connect(m_waitTimer, &QTimer::timeout, this, [this]() {
        if (!processExists(m_session.oldProcessId)) {
            m_waitTimer->stop();
            emitProgress(1,
                         8,
                         QStringLiteral("关闭旧版本"),
                         QStringLiteral("旧版本已退出。"),
                         QStringLiteral("正在启动独立安装程序..."));
            QTimer::singleShot(600, this, &UpdaterSessionRunner::startSilentInstaller);
            return;
        }
        const auto elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_waitStartedAtMs;
        const auto elapsedSecond = elapsedMs / 1000;
        if (elapsedSecond != m_lastWaitDetailSecond) {
            m_lastWaitDetailSecond = elapsedSecond;
            emitProgress(1,
                         5,
                         QStringLiteral("关闭旧版本"),
                         QStringLiteral("正在等待旧版本安全退出..."),
                         QStringLiteral("已等待 %1 秒 · 正在检查 PID %2")
                             .arg(elapsedSecond)
                             .arg(m_session.oldProcessId));
        }
        if (elapsedMs >= kOldProcessWaitTimeoutMs) {
            m_waitTimer->stop();
            completeFailure(QStringLiteral("等待旧版本退出超时。"),
                            QStringLiteral("请关闭影资管家后重新运行更新。"));
        }
    });
    m_waitTimer->start();
}

void UpdaterSessionRunner::startSilentInstaller()
{
    if (m_finished) {
        return;
    }

    const QFileInfo installerInfo(m_session.installerPath);
    const auto installerSizeMiB = static_cast<double>(installerInfo.size()) / (1024.0 * 1024.0);
    emitProgress(2,
                 kInstallProgressStart,
                 QStringLiteral("安装新版本"),
                 QStringLiteral("正在启动静默安装程序..."),
                 QStringLiteral("安装包：%1（%2 MiB） · 目标：%3 · 系统可能请求管理员权限")
                     .arg(installerInfo.fileName())
                     .arg(installerSizeMiB, 0, 'f', 1)
                     .arg(QDir::toNativeSeparators(m_session.installRoot)));

    const auto root = sessionRoot(safeSessionId(m_session.sessionId));
    if (!QDir().mkpath(root)) {
        completeFailure(QStringLiteral("无法创建更新会话日志目录：%1").arg(root));
        return;
    }

    const auto installerLogPath = QDir(root).filePath(QStringLiteral("installer.log"));
    const auto updaterLogPath = QDir(root).filePath(QStringLiteral("updater.log"));
    m_installProgressFilePath = QDir(root).filePath(QStringLiteral("install-progress.txt"));
    QFile::remove(m_installProgressFilePath);
    const auto scriptPath = QDir(root).filePath(QStringLiteral("install-update.ps1"));
    const QStringList scriptLines{
        QStringLiteral("$ErrorActionPreference = 'Stop'"),
        QStringLiteral("$installerPath = %1").arg(powerShellLiteral(m_session.installerPath)),
        QStringLiteral("$appDir = %1").arg(powerShellLiteral(m_session.installRoot)),
        QStringLiteral("$installerLogPath = %1").arg(powerShellLiteral(installerLogPath)),
        QStringLiteral("$installProgressPath = %1").arg(powerShellLiteral(m_installProgressFilePath)),
        QStringLiteral("$scriptLogPath = %1").arg(powerShellLiteral(updaterLogPath)),
        QStringLiteral("function Write-UpdateLog([string]$message) {"),
        QStringLiteral("    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'"),
        QStringLiteral("    Add-Content -LiteralPath $scriptLogPath -Value ($timestamp + ' ' + $message) -Encoding UTF8"),
        QStringLiteral("}"),
        QStringLiteral("try {"),
        QStringLiteral("    Write-UpdateLog '可视化更新器开始静默安装，目标版本：%1'").arg(m_session.versionTag),
        QStringLiteral("    $installerArgs = @("),
        QStringLiteral("        '/SP-',"),
        QStringLiteral("        '/VERYSILENT',"),
        QStringLiteral("        '/SUPPRESSMSGBOXES',"),
        QStringLiteral("        '/NORESTART',"),
        QStringLiteral("        '/NOCANCEL',"),
        QStringLiteral("        '/CLOSEAPPLICATIONS',"),
        QStringLiteral("        '/FORCECLOSEAPPLICATIONS',"),
        QStringLiteral("        ('/DIR=\"' + $appDir + '\"'),"),
        QStringLiteral("        ('/LOG=\"' + $installerLogPath + '\"'),"),
        QStringLiteral("        ('/UPDATEPROGRESSFILE=\"' + $installProgressPath + '\"')"),
        QStringLiteral("    )"),
        QStringLiteral("    $process = Start-Process -FilePath $installerPath -ArgumentList $installerArgs -Verb RunAs -Wait -PassThru"),
        QStringLiteral("    Write-UpdateLog ('安装进程已结束，ExitCode=' + $process.ExitCode)"),
        QStringLiteral("    exit $process.ExitCode"),
        QStringLiteral("} catch {"),
        QStringLiteral("    Write-UpdateLog ('安装脚本失败：' + $_.Exception.Message)"),
        QStringLiteral("    exit 1"),
        QStringLiteral("}")
    };

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        completeFailure(QStringLiteral("无法创建静默安装脚本：%1").arg(scriptPath));
        return;
    }
    scriptFile.write("\xEF\xBB\xBF");
    scriptFile.write(scriptLines.join(QStringLiteral("\r\n")).toUtf8());
    scriptFile.close();

    m_installerProcess = new QProcess(this);
#if defined(Q_OS_WIN)
    m_installerProcess->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_installerProcess->setProgram(QStringLiteral("powershell.exe"));
    m_installerProcess->setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-WindowStyle"),
        QStringLiteral("Hidden"),
        QStringLiteral("-File"),
        QDir::toNativeSeparators(scriptPath)
    });
    m_installerProcess->setWorkingDirectory(root);
    connect(m_installerProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                handleInstallerFinished(exitStatus == QProcess::NormalExit ? exitCode : -1);
            });
    connect(m_installerProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            completeFailure(QStringLiteral("无法启动静默安装进程。"));
        }
    });
    m_installerProcess->start();

    m_installProgressTimer = new QTimer(this);
    m_installProgressTimer->setInterval(150);
    connect(m_installProgressTimer,
            &QTimer::timeout,
            this,
            &UpdaterSessionRunner::pollInstallerProgress);
    m_installProgressTimer->start();
}

void UpdaterSessionRunner::pollInstallerProgress()
{
    QFile progressFile(m_installProgressFilePath);
    if (!progressFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const int installerProgress = parseInstallerProgress(progressFile.readAll());
    if (installerProgress < 0) {
        return;
    }

    const int overallProgress = overallProgressForInstallerProgress(installerProgress);
    if (overallProgress <= m_lastPercentage) {
        return;
    }

    emitProgress(2,
                 overallProgress,
                 QStringLiteral("安装新版本"),
                 QStringLiteral("正在写入程序文件并配置组件..."),
                 QStringLiteral("安装器实际进度 %1% · 总进度目标 %2%")
                     .arg(installerProgress)
                     .arg(overallProgress));
}

void UpdaterSessionRunner::handleInstallerFinished(int exitCode)
{
    if (m_finished) {
        return;
    }
    if (m_installProgressTimer) {
        m_installProgressTimer->stop();
    }
    if (exitCode != 0) {
        completeFailure(QStringLiteral("安装程序退出码：%1").arg(exitCode),
                        QStringLiteral("安装包和会话日志已保留，可重新打开影资管家后重试。"));
        return;
    }

    emitProgress(3,
                 95,
                 QStringLiteral("启动新版本"),
                 QStringLiteral("安装完成，正在启动新版本..."),
                 QStringLiteral("正在等待新版主程序可用。"));
    QTimer::singleShot(800, this, &UpdaterSessionRunner::completeSuccess);
}

void UpdaterSessionRunner::completeFailure(const QString &message, const QString &substep)
{
    if (m_finished) {
        return;
    }
    if (m_installProgressTimer) {
        m_installProgressTimer->stop();
    }
    m_finished = true;
    emitProgress(2,
                 m_lastPercentage,
                 QStringLiteral("安装新版本"),
                 message,
                 substep,
                 true,
                 false);
    emit finished(false, message);
}

void UpdaterSessionRunner::completeSuccess()
{
    if (m_finished) {
        return;
    }
    const auto executablePath = QDir(m_session.installRoot).filePath(m_session.executableName);
    if (!QFileInfo::exists(executablePath)) {
        completeFailure(QStringLiteral("新版主程序不存在：%1").arg(executablePath));
        return;
    }
    if (!QProcess::startDetached(executablePath, {}, m_session.installRoot)) {
        completeFailure(QStringLiteral("无法启动新版影资管家。"),
                        QStringLiteral("可从安装目录手动启动程序。"));
        return;
    }

    m_finished = true;
    emitProgress(4,
                 100,
                 QStringLiteral("完成"),
                 QStringLiteral("已启动 %1").arg(m_session.versionTag),
                 QStringLiteral("更新完成。"),
                 false,
                 true);
    emit finished(true, QStringLiteral("更新完成。"));
}
