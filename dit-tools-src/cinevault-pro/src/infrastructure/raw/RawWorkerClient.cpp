#include "infrastructure/raw/RawWorkerClient.h"

#include "infrastructure/raw/RawPreviewProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonValue>
#include <QJsonArray>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

RawWorkerReply clientError(const QString &code, const QString &message, bool retryable)
{
    RawWorkerReply reply;
    reply.errorCode = code;
    reply.errorMessage = message;
    reply.retryable = retryable;
    return reply;
}

QString configuredWorkerPath()
{
    return QDir::fromNativeSeparators(
        QString::fromLocal8Bit(qgetenv("CINEVAULT_RAW_WORKER_PATH")).trimmed());
}

} // namespace

class RawWorkerSession final : public QObject {
public:
    RawWorkerSession(QString executablePath, QStringList arguments, int requestTimeoutMs)
        : m_executablePath(std::move(executablePath))
        , m_arguments(std::move(arguments))
        , m_requestTimeoutMs(qMax(100, requestTimeoutMs))
        , m_process(new QProcess(this))
    {
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
        m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        });
#endif
    }

    RawWorkerReply sendRequest(const QString &command,
                               const QJsonObject &payload,
                               int timeoutOverrideMs)
    {
        if (command.trimmed().isEmpty()) {
            return clientError(QStringLiteral("invalid_request"),
                               QStringLiteral("RAW worker 命令不能为空"),
                               false);
        }

        QString startupError;
        if (!ensureStarted(&startupError)) {
            return clientError(QStringLiteral("worker_unavailable"), startupError, true);
        }

        const auto requestTimeoutMs = timeoutOverrideMs > 0
            ? qBound(100, timeoutOverrideMs, m_requestTimeoutMs)
            : m_requestTimeoutMs;
        const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject request = {
            {QStringLiteral("protocolVersion"), RawPreviewProtocol::ProtocolVersion},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("command"), command},
            {QStringLiteral("payload"), payload},
        };
        QString protocolError;
        const auto frame = RawPreviewProtocol::encodeMessage(request, &protocolError);
        if (frame.isEmpty()) {
            return clientError(QStringLiteral("invalid_request"), protocolError, false);
        }
        if (m_process->write(frame) != frame.size()) {
            return restartAfterError(QStringLiteral("write_failed"),
                                     QStringLiteral("无法向 RAW worker 写入请求：%1")
                                         .arg(m_process->errorString()));
        }
        if (!m_process->waitForBytesWritten(qMin(3000, requestTimeoutMs))) {
            return restartAfterError(QStringLiteral("write_timeout"),
                                     QStringLiteral("向 RAW worker 写入请求超时"));
        }

        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < requestTimeoutMs) {
            QJsonObject response;
            const auto status = RawPreviewProtocol::tryTakeMessage(
                &m_stdoutBuffer, &response, &protocolError);
            if (status == RawPreviewProtocol::ReadStatus::InvalidFrame) {
                return restartAfterError(QStringLiteral("protocol_error"), protocolError);
            }
            if (status == RawPreviewProtocol::ReadStatus::MessageReady) {
                return parseResponse(requestId, response);
            }

            const auto remaining = requestTimeoutMs - static_cast<int>(elapsed.elapsed());
            if (remaining <= 0) {
                break;
            }
            m_process->waitForReadyRead(qMin(remaining, 250));
            m_stdoutBuffer.append(m_process->readAllStandardOutput());
            appendDiagnostic(m_process->readAllStandardError());
            if (m_process->state() == QProcess::NotRunning) {
                return restartAfterError(
                    QStringLiteral("worker_exited"),
                    QStringLiteral("RAW worker 意外退出（exit=%1, status=%2）：%3")
                        .arg(m_process->exitCode())
                        .arg(static_cast<int>(m_process->exitStatus()))
                        .arg(lastDiagnostic()));
            }
        }

        return restartAfterError(QStringLiteral("timeout"),
                                 QStringLiteral("RAW worker 请求超过 %1 毫秒，旧进程已终止")
                                     .arg(requestTimeoutMs));
    }

    void stop()
    {
        m_stdoutBuffer.clear();
        m_stderrBuffer.clear();
        if (m_process->state() == QProcess::NotRunning) {
            return;
        }
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }

    [[nodiscard]] bool isRunning() const
    {
        return m_process->state() != QProcess::NotRunning;
    }

    [[nodiscard]] quint64 restartCount() const
    {
        return m_restartCount;
    }

private:
    bool ensureStarted(QString *errorMessage)
    {
        if (m_process->state() != QProcess::NotRunning) {
            return true;
        }
        if (!QFileInfo(m_executablePath).isFile()) {
            *errorMessage = QStringLiteral("RAW worker 不存在：%1").arg(m_executablePath);
            return false;
        }

        m_stdoutBuffer.clear();
        m_stderrBuffer.clear();
        m_process->setProgram(QDir::toNativeSeparators(m_executablePath));
        m_process->setArguments(m_arguments);
        m_process->setWorkingDirectory(QFileInfo(m_executablePath).absolutePath());
        m_process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        m_process->start();
        if (!m_process->waitForStarted(3000)) {
            *errorMessage = QStringLiteral("RAW worker 启动失败：%1").arg(m_process->errorString());
            return false;
        }
        return true;
    }

    RawWorkerReply parseResponse(const QString &expectedRequestId, const QJsonObject &response)
    {
        if (response.value(QStringLiteral("protocolVersion")).toInt()
                != RawPreviewProtocol::ProtocolVersion
            || response.value(QStringLiteral("requestId")).toString() != expectedRequestId
            || !response.value(QStringLiteral("ok")).isBool()) {
            return restartAfterError(QStringLiteral("protocol_error"),
                                     QStringLiteral("RAW worker 响应版本或请求 ID 不匹配"));
        }

        RawWorkerReply reply;
        reply.requestId = expectedRequestId;
        reply.ok = response.value(QStringLiteral("ok")).toBool();
        if (reply.ok) {
            reply.result = response.value(QStringLiteral("result")).toObject();
            return reply;
        }

        const auto error = response.value(QStringLiteral("error")).toObject();
        reply.errorCode = error.value(QStringLiteral("code")).toString();
        reply.errorMessage = error.value(QStringLiteral("message")).toString();
        reply.retryable = error.value(QStringLiteral("retryable")).toBool();
        if (reply.errorCode.isEmpty() || reply.errorMessage.isEmpty()) {
            return restartAfterError(QStringLiteral("protocol_error"),
                                     QStringLiteral("RAW worker 错误响应缺少必要字段"));
        }
        return reply;
    }

    RawWorkerReply restartAfterError(const QString &code, const QString &message)
    {
        stop();
        ++m_restartCount;
        QString restartError;
        const auto restarted = ensureStarted(&restartError);
        auto reply = clientError(code,
                                 restarted || restartError.isEmpty()
                                     ? message
                                     : QStringLiteral("%1；重启失败：%2").arg(message, restartError),
                                 true);
        return reply;
    }

    void appendDiagnostic(const QByteArray &diagnostic)
    {
        if (diagnostic.isEmpty()) {
            return;
        }
        m_stderrBuffer.append(diagnostic);
        constexpr qsizetype MaximumDiagnosticBytes = 4096;
        if (m_stderrBuffer.size() > MaximumDiagnosticBytes) {
            m_stderrBuffer = m_stderrBuffer.right(MaximumDiagnosticBytes);
        }
    }

    QString lastDiagnostic() const
    {
        const auto diagnostic = QString::fromLocal8Bit(m_stderrBuffer).simplified();
        return diagnostic.isEmpty() ? QStringLiteral("无诊断信息") : diagnostic;
    }

    QString m_executablePath;
    QStringList m_arguments;
    int m_requestTimeoutMs = 20000;
    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    quint64 m_restartCount = 0;
};

RawWorkerClient::RawWorkerClient(QString executablePath,
                                 QStringList arguments,
                                 int requestTimeoutMs)
    : m_session(new RawWorkerSession(
          executablePath.trimmed().isEmpty() ? defaultExecutablePath() : executablePath,
          std::move(arguments),
          requestTimeoutMs))
{
    m_session->moveToThread(&m_ioThread);
    m_ioThread.setObjectName(QStringLiteral("CineVaultRawWorkerIo"));
    m_ioThread.start();
}

RawWorkerClient::~RawWorkerClient()
{
    if (m_session && m_ioThread.isRunning()) {
        auto *session = m_session;
        QMetaObject::invokeMethod(session,
                                  [session]() {
            session->stop();
            delete session;
        },
                                  Qt::BlockingQueuedConnection);
        m_session = nullptr;
    }
    m_ioThread.quit();
    m_ioThread.wait(3000);
}

RawWorkerReply RawWorkerClient::sendRequest(const QString &command,
                                            const QJsonObject &payload,
                                            int timeoutOverrideMs)
{
    RawWorkerReply reply;
    QMetaObject::invokeMethod(m_session,
                              [this, &reply, command, payload, timeoutOverrideMs]() {
        reply = m_session->sendRequest(command, payload, timeoutOverrideMs);
    },
                              Qt::BlockingQueuedConnection);
    return reply;
}

RawWorkerReply RawWorkerClient::decode(QJsonObject payload)
{
    constexpr int DecodeWatchdogTimeoutMs = 90000;
    QStringList providers;
    const auto sourcePath = payload.value(QStringLiteral("sourcePath")).toString();
    if (QFileInfo(sourcePath).suffix().compare(QStringLiteral("gpr"), Qt::CaseInsensitive) == 0) {
        providers.append(QStringLiteral("gopro_gpr_sdk"));
    }
    providers.append({
        QStringLiteral("libraw_embedded"),
        QStringLiteral("libraw_rendered"),
        QStringLiteral("exiftool_embedded"),
        QStringLiteral("wic"),
        QStringLiteral("ffmpeg"),
    });
    QJsonArray recoveryAttempts;
    RawWorkerReply reply;
    QElapsedTimer watchdog;
    watchdog.start();
    for (int providerIndex = 0; providerIndex < providers.size(); ++providerIndex) {
        const auto remainingMs = DecodeWatchdogTimeoutMs
            - static_cast<int>(watchdog.elapsed());
        if (remainingMs <= 0) {
            return clientError(QStringLiteral("timeout"),
                               QStringLiteral("RAW 自动恢复超过 90 秒，已中断并等待重试"),
                               true);
        }
        payload.insert(QStringLiteral("providerStartIndex"), providerIndex);
        reply = sendRequest(QStringLiteral("decode"), payload, remainingMs);
        if (reply.ok) {
            auto attempts = recoveryAttempts;
            for (const auto &attempt : reply.result.value(QStringLiteral("attempts")).toArray()) {
                attempts.append(attempt);
            }
            reply.result.insert(QStringLiteral("attempts"), attempts);
            return reply;
        }
        const bool processFailure = reply.errorCode == QStringLiteral("worker_exited")
            || reply.errorCode == QStringLiteral("timeout")
            || reply.errorCode == QStringLiteral("protocol_error")
            || reply.errorCode == QStringLiteral("write_failed")
            || reply.errorCode == QStringLiteral("write_timeout");
        if (!processFailure) {
            return reply;
        }
        recoveryAttempts.append(QJsonObject{
            {QStringLiteral("provider"), providers.at(providerIndex)},
            {QStringLiteral("error"), reply.errorMessage.left(500)},
            {QStringLiteral("workerRestarted"), true},
        });
    }
    return reply;
}

void RawWorkerClient::stop()
{
    QMetaObject::invokeMethod(m_session,
                              [this]() { m_session->stop(); },
                              Qt::BlockingQueuedConnection);
}

bool RawWorkerClient::isRunning() const
{
    bool running = false;
    QMetaObject::invokeMethod(m_session,
                              [this, &running]() { running = m_session->isRunning(); },
                              Qt::BlockingQueuedConnection);
    return running;
}

quint64 RawWorkerClient::restartCount() const
{
    quint64 count = 0;
    QMetaObject::invokeMethod(m_session,
                              [this, &count]() { count = m_session->restartCount(); },
                              Qt::BlockingQueuedConnection);
    return count;
}

QString RawWorkerClient::defaultExecutablePath()
{
    const auto configured = configuredWorkerPath();
    if (!configured.isEmpty()) {
        return configured;
    }
#ifdef Q_OS_WIN
    constexpr auto WorkerName = "CineVaultRawWorker.exe";
#else
    constexpr auto WorkerName = "CineVaultRawWorker";
#endif
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QString::fromLatin1(WorkerName));
}
