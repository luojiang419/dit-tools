#include "application/FeedbackService.h"
#include "application/ProjectService.h"
#include "infrastructure/config/AppSettings.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

Project ProjectService::currentProject() const
{
    return {};
}

bool ProjectService::hasOpenProject() const
{
    return false;
}

namespace {
struct CacheResult {
    bool called = false;
    bool success = false;
    QString localPath;
    QString errorMessage;
};

class ScopedNoProxy {
public:
    ScopedNoProxy()
        : m_previousProxy(QNetworkProxy::applicationProxy())
        , m_previousUseSystemConfiguration(QNetworkProxyFactory::usesSystemConfiguration())
    {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }

    ~ScopedNoProxy()
    {
        QNetworkProxy::setApplicationProxy(m_previousProxy);
        QNetworkProxyFactory::setUseSystemConfiguration(m_previousUseSystemConfiguration);
    }

private:
    QNetworkProxy m_previousProxy;
    bool m_previousUseSystemConfiguration = false;
};

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString uniqueAttachmentId(const QString &prefix)
{
    return QStringLiteral("%1-%2")
        .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

CacheResult ensureAttachmentCachedAndWait(FeedbackService &service,
                                          const QString &attachmentId,
                                          const QString &url,
                                          const QString &name)
{
    CacheResult result;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    service.ensureAttachmentCached(attachmentId, url, name, [&](bool success, const QString &localPath, const QString &errorMessage) {
        result.called = true;
        result.success = success;
        result.localPath = localPath;
        result.errorMessage = errorMessage;
        loop.quit();
    });

    if (!result.called) {
        timeout.start();
        loop.exec();
    }
    return result;
}

class TinyHttpFileServer {
public:
    explicit TinyHttpFileServer(const QByteArray &payload)
        : m_payload(payload)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (m_server.hasPendingConnections()) {
                auto *socket = m_server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    respondIfRequestReady(socket);
                });
                respondIfRequestReady(socket);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2")
                        .arg(m_server.serverPort())
                        .arg(path));
    }

    int requestCount() const
    {
        return m_requestCount;
    }

private:
    void respondIfRequestReady(QTcpSocket *socket)
    {
        if (!socket || socket->property("responded").toBool() || socket->bytesAvailable() <= 0) {
            return;
        }

        socket->setProperty("responded", true);
        socket->readAll();
        ++m_requestCount;

        const QByteArray headers = QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                                                     "Content-Type: application/octet-stream\r\n"
                                                     "Connection: close\r\nContent-Length: ")
            + QByteArray::number(m_payload.size())
            + QByteArrayLiteral("\r\n\r\n");
        socket->write(headers);
        socket->write(m_payload);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QByteArray m_payload;
    int m_requestCount = 0;
};
}

class FeedbackAttachmentCacheTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void ensureAttachmentCached_reusesExistingCachedFile();
    void ensureAttachmentCached_copiesLocalSourceIntoCache();
    void ensureAttachmentCached_downloadsRemoteSourceIntoCache();
    void ensureAttachmentCached_reportsFailureForMissingSource();
};

void FeedbackAttachmentCacheTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CineVaultTests"));
    QCoreApplication::setApplicationName(QStringLiteral("FeedbackAttachmentCacheTest"));
}

void FeedbackAttachmentCacheTest::init()
{
    QDir(Paths::cacheRoot()).removeRecursively();
}

void FeedbackAttachmentCacheTest::cleanup()
{
    QDir(Paths::cacheRoot()).removeRecursively();
}

void FeedbackAttachmentCacheTest::ensureAttachmentCached_reusesExistingCachedFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("notes.txt"));
    writeFile(sourcePath, "first-version");

    AppSettings settings;
    settings.setFeedbackSessionJson(QString());
    settings.sync();
    FeedbackService service(&settings, nullptr);

    const auto attachmentId = uniqueAttachmentId(QStringLiteral("reuse"));
    const auto attachmentUrl = QUrl::fromLocalFile(sourcePath).toString();

    const auto firstResult = ensureAttachmentCachedAndWait(service, attachmentId, attachmentUrl, QStringLiteral("notes.txt"));
    QVERIFY(firstResult.called);
    QVERIFY(firstResult.success);
    QCOMPARE(readFile(firstResult.localPath), QByteArray("first-version"));

    writeFile(sourcePath, "second-version");

    const auto secondResult = ensureAttachmentCachedAndWait(service, attachmentId, attachmentUrl, QStringLiteral("notes.txt"));
    QVERIFY(secondResult.called);
    QVERIFY(secondResult.success);
    QCOMPARE(secondResult.localPath, firstResult.localPath);
    QCOMPARE(readFile(secondResult.localPath), QByteArray("first-version"));
}

void FeedbackAttachmentCacheTest::ensureAttachmentCached_copiesLocalSourceIntoCache()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("report.md"));
    writeFile(sourcePath, "# cached\n");

    AppSettings settings;
    settings.setFeedbackSessionJson(QString());
    settings.sync();
    FeedbackService service(&settings, nullptr);

    const auto result = ensureAttachmentCachedAndWait(service,
                                                      uniqueAttachmentId(QStringLiteral("local")),
                                                      QUrl::fromLocalFile(sourcePath).toString(),
                                                      QStringLiteral("report.md"));
    QVERIFY(result.called);
    QVERIFY(result.success);
    QVERIFY(QFileInfo(result.localPath).exists());
    QVERIFY(QFileInfo(result.localPath).absoluteFilePath() != QFileInfo(sourcePath).absoluteFilePath());
    QCOMPARE(readFile(result.localPath), QByteArray("# cached\n"));
    QCOMPARE(QDir::cleanPath(QFileInfo(result.localPath).absolutePath()),
             QDir::cleanPath(QDir(Paths::cacheRoot()).filePath(QStringLiteral("feedback-attachments"))));
}

void FeedbackAttachmentCacheTest::ensureAttachmentCached_downloadsRemoteSourceIntoCache()
{
    ScopedNoProxy noProxy;
    TinyHttpFileServer server(QByteArray("remote-pdf"));
    QVERIFY(server.listen());

    AppSettings settings;
    settings.setFeedbackSessionJson(QString());
    settings.sync();
    FeedbackService service(&settings, nullptr);

    const auto result = ensureAttachmentCachedAndWait(service,
                                                      uniqueAttachmentId(QStringLiteral("remote")),
                                                      server.url(QStringLiteral("manual.pdf")).toString(),
                                                      QStringLiteral("manual.pdf"));
    QVERIFY(result.called);
    QVERIFY(result.success);
    QCOMPARE(readFile(result.localPath), QByteArray("remote-pdf"));
    QCOMPARE(server.requestCount(), 1);
}

void FeedbackAttachmentCacheTest::ensureAttachmentCached_reportsFailureForMissingSource()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto missingPath = QDir(tempDir.path()).filePath(QStringLiteral("missing.pdf"));

    AppSettings settings;
    settings.setFeedbackSessionJson(QString());
    settings.sync();
    FeedbackService service(&settings, nullptr);

    const auto result = ensureAttachmentCachedAndWait(service,
                                                      uniqueAttachmentId(QStringLiteral("missing")),
                                                      QUrl::fromLocalFile(missingPath).toString(),
                                                      QStringLiteral("missing.pdf"));
    QVERIFY(result.called);
    QVERIFY(!result.success);
    QVERIFY(result.localPath.isEmpty());
    QVERIFY(!result.errorMessage.isEmpty());
}

QTEST_GUILESS_MAIN(FeedbackAttachmentCacheTest)

#include "FeedbackAttachmentCacheTest.moc"
