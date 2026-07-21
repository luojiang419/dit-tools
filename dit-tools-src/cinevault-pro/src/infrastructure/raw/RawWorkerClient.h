#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QThread>

struct RawWorkerReply {
    QString requestId;
    bool ok = false;
    QJsonObject result;
    QString errorCode;
    QString errorMessage;
    bool retryable = false;
};

class RawWorkerSession;

class RawWorkerClient final {
public:
    explicit RawWorkerClient(QString executablePath = {},
                             QStringList arguments = {},
                             int requestTimeoutMs = 20000);
    ~RawWorkerClient();

    RawWorkerClient(const RawWorkerClient &) = delete;
    RawWorkerClient &operator=(const RawWorkerClient &) = delete;

    RawWorkerReply sendRequest(const QString &command, const QJsonObject &payload = {});
    RawWorkerReply decode(QJsonObject payload);
    void stop();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] quint64 restartCount() const;

    static QString defaultExecutablePath();

private:
    QThread m_ioThread;
    RawWorkerSession *m_session = nullptr;
};
