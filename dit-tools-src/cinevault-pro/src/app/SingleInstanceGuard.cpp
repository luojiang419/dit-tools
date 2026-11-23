#include "app/SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QString instanceStorageRoot()
{
    auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        root = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                   .filePath(QStringLiteral("CineVault"));
    }
    return QDir::cleanPath(root);
}
}

SingleInstanceGuard::SingleInstanceGuard(const QString &applicationId, QObject *parent)
    : QObject(parent)
{
    const auto storageRoot = instanceStorageRoot();
    const auto identity = applicationId.trimmed().isEmpty()
        ? QStringLiteral("cinevault")
        : applicationId.trimmed();
    const auto digest = QCryptographicHash::hash(
                            QStringLiteral("%1\n%2").arg(identity, storageRoot).toUtf8(),
                            QCryptographicHash::Sha256)
                            .toHex()
                            .left(24);
    const auto instanceKey = QString::fromLatin1(digest);

    m_lockFilePath = QDir(storageRoot).filePath(
        QStringLiteral("instance-%1.lock").arg(instanceKey));
    m_serverName = QStringLiteral("CineVault-%1").arg(instanceKey);
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_primary && m_server) {
        m_server->close();
        QLocalServer::removeServer(m_serverName);
    }
    if (m_primary && m_lockFile) {
        m_lockFile->unlock();
    }
}

SingleInstanceGuard::StartResult SingleInstanceGuard::start(QString *errorMessage)
{
    if (m_primary) {
        return StartResult::PrimaryInstance;
    }

    if (!QDir().mkpath(QFileInfo(m_lockFilePath).absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建单实例锁目录：%1")
                                .arg(QFileInfo(m_lockFilePath).absolutePath());
        }
        return StartResult::Failed;
    }

    if (!m_lockFile) {
        m_lockFile = std::make_unique<QLockFile>(m_lockFilePath);
        m_lockFile->setStaleLockTime(0);
    }

    if (m_lockFile->tryLock(0)) {
        return beginListening(errorMessage)
            ? StartResult::PrimaryInstance
            : StartResult::Failed;
    }

    if (notifyExistingInstance()) {
        return StartResult::SecondaryInstanceNotified;
    }

    if (m_lockFile->removeStaleLockFile() && m_lockFile->tryLock(0)) {
        return beginListening(errorMessage)
            ? StartResult::PrimaryInstance
            : StartResult::Failed;
    }

    if (notifyExistingInstance()) {
        return StartResult::SecondaryInstanceNotified;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("检测到已有影资管家实例，但无法通知其显示主窗口。请稍后重试。");
    }
    return StartResult::Failed;
}

bool SingleInstanceGuard::isPrimary() const
{
    return m_primary;
}

bool SingleInstanceGuard::beginListening(QString *errorMessage)
{
    QLocalServer::removeServer(m_serverName);
    m_server = std::make_unique<QLocalServer>();
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server.get(),
            &QLocalServer::newConnection,
            this,
            &SingleInstanceGuard::handleNewConnection);

    if (!m_server->listen(m_serverName)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法启动单实例通信通道：%1")
                                .arg(m_server->errorString());
        }
        m_server.reset();
        m_lockFile->unlock();
        return false;
    }

    m_primary = true;
    return true;
}

bool SingleInstanceGuard::notifyExistingInstance() const
{
#ifdef Q_OS_WIN
    qint64 processId = 0;
    QString hostName;
    QString applicationName;
    if (m_lockFile
        && m_lockFile->getLockInfo(&processId, &hostName, &applicationName)
        && processId > 0) {
        AllowSetForegroundWindow(static_cast<DWORD>(processId));
    }
#endif

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) {
        QLocalSocket socket;
        socket.connectToServer(m_serverName, QIODevice::WriteOnly);
        if (socket.waitForConnected(150)) {
            socket.write("activate\n");
            socket.flush();
            socket.waitForBytesWritten(250);
            socket.disconnectFromServer();
            return true;
        }
        QThread::msleep(50);
    }
    return false;
}

void SingleInstanceGuard::handleNewConnection()
{
    bool shouldActivate = false;
    while (m_server && m_server->hasPendingConnections()) {
        auto *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        socket->readAll();
        socket->disconnectFromServer();
        socket->deleteLater();
        shouldActivate = true;
    }

    if (shouldActivate) {
        emit activationRequested();
    }
}
