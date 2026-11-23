#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLockFile;

class SingleInstanceGuard final : public QObject {
    Q_OBJECT

public:
    enum class StartResult {
        PrimaryInstance,
        SecondaryInstanceNotified,
        Failed,
    };

    explicit SingleInstanceGuard(const QString &applicationId = QStringLiteral("cinevault"),
                                 QObject *parent = nullptr);
    ~SingleInstanceGuard() override;

    StartResult start(QString *errorMessage = nullptr);
    bool isPrimary() const;

signals:
    void activationRequested();

private:
    bool beginListening(QString *errorMessage);
    bool notifyExistingInstance() const;
    void handleNewConnection();

    QString m_lockFilePath;
    QString m_serverName;
    std::unique_ptr<QLockFile> m_lockFile;
    std::unique_ptr<QLocalServer> m_server;
    bool m_primary = false;
};
