#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QThreadPool>

#include <functional>

class MaterialCenterQueryService;
class QTcpServer;
class QTcpSocket;
class QUrl;

class WebSearchService final : public QObject {
    Q_OBJECT

public:
    explicit WebSearchService(MaterialCenterQueryService *queryService,
                              QObject *parent = nullptr);
    ~WebSearchService() override;

    bool start(quint16 preferredPort = 17890, QString *errorMessage = nullptr);
    void stop();

    bool isRunning() const;
    quint16 port() const;
    QStringList localAccessUrls() const;

private:
    struct AsyncResponse {
        int statusCode = 500;
        QByteArray contentType = "application/json; charset=utf-8";
        QByteArray body;
    };

    void handleNewConnection();
    void handleReadyRead(QTcpSocket *socket);
    void routeRequest(QTcpSocket *socket, const QByteArray &requestBytes);
    void sendStaticFile(QTcpSocket *socket, const QString &resourcePath, const QByteArray &contentType) const;
    void sendSearchResponse(QTcpSocket *socket, const QUrl &url);
    void sendVideoDetailResponse(QTcpSocket *socket, const QUrl &url);
    void sendThumbnailResponse(QTcpSocket *socket, const QUrl &url);
    void runAsyncResponse(QTcpSocket *socket, std::function<AsyncResponse()> operation);
    void sendHealthResponse(QTcpSocket *socket) const;
    void sendJson(QTcpSocket *socket, int statusCode, const QByteArray &body) const;
    void sendResponse(QTcpSocket *socket,
                      int statusCode,
                      const QByteArray &reason,
                      const QByteArray &contentType,
                      const QByteArray &body) const;

    MaterialCenterQueryService *m_queryService = nullptr;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_requestBuffers;
    QThreadPool m_queryPool;
    int m_inFlightQueries = 0;
};
