#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QProcess;

class LocalSearchAssistantRuntime : public QObject {
    Q_OBJECT

public:
    explicit LocalSearchAssistantRuntime(QString executablePath = {},
                                         QString modelPath = {},
                                         QObject *parent = nullptr);
    ~LocalSearchAssistantRuntime() override;

    bool start();
    void stop();

    [[nodiscard]] bool assetsAvailable() const;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool isStarting() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString modelName() const;
    [[nodiscard]] QString executablePath() const;
    [[nodiscard]] QString modelPath() const;
    [[nodiscard]] QString gpuDeviceName() const;
    [[nodiscard]] QString lastError() const;

    static QString defaultExecutablePath();
    static QString defaultModelPath();

signals:
    void ready();
    void failed(const QString &errorMessage);
    void statusChanged();

private:
    enum class State {
        Stopped,
        GpuProbing,
        Starting,
        Stopping,
        Ready,
        Failed
    };

    void startGpuProbe();
    void launchRuntime();
    void beginStop(bool restartAfterStop);
    void maybeRestartAfterStop();
    void probeHealth();
    void fail(const QString &message);
    void appendDiagnostic(const QByteArray &output);

    QString m_executablePath;
    QString m_modelPath;
    QString m_endpoint;
    QString m_gpuDeviceName;
    QString m_lastError;
    QString m_lastDiagnostic;
    QString m_modelName = QStringLiteral("cinevault-qwen3-0.6b");
    State m_state = State::Stopped;
    QProcess *m_process = nullptr;
    QProcess *m_gpuProbeProcess = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QTimer m_probeTimer;
    QTimer m_gpuProbeTimeout;
    QTimer m_stopKillTimer;
    QElapsedTimer m_startElapsed;
    bool m_probeInFlight = false;
    bool m_restartAfterStop = false;
};
