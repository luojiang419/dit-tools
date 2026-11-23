#include "application/websearch/WebSearchService.h"

#include "application/MaterialCenterReadContext.h"
#include "application/MaterialCenterQueryService.h"
#include "domain/Enums.h"
#include "domain/SearchTypes.h"
#include "shared/Formatters.h"
#include "shared/Paths.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {
constexpr quint16 kDefaultPort = 17890;
constexpr quint16 kMaxPort = 17899;
constexpr qsizetype kDefaultLimit = 80;
constexpr qsizetype kMaxLimit = 200;
constexpr qsizetype kMaxRequestHeaderBytes = 32 * 1024;
constexpr int kMaxInFlightQueries = 8;
constexpr int kRequestHeaderTimeoutMs = 5000;

QByteArray reasonPhrase(int statusCode)
{
    switch (statusCode) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Content Too Large";
    case 503: return "Service Unavailable";
    default: return "Internal Server Error";
    }
}

QString statusText(VideoAnalysisStatus status)
{
    switch (status) {
    case VideoAnalysisStatus::Pending: return QStringLiteral("待解析");
    case VideoAnalysisStatus::Running: return QStringLiteral("解析中");
    case VideoAnalysisStatus::Ready: return QStringLiteral("已解析");
    case VideoAnalysisStatus::Failed: return QStringLiteral("解析失败");
    case VideoAnalysisStatus::IndexedOnly: return QStringLiteral("仅索引");
    }
    return QStringLiteral("未知");
}

QString frameStatusText(FrameAnalysisState status)
{
    switch (status) {
    case FrameAnalysisState::Pending: return QStringLiteral("待解析");
    case FrameAnalysisState::Success: return QStringLiteral("已完成");
    case FrameAnalysisState::Failed: return QStringLiteral("解析失败");
    case FrameAnalysisState::Skipped: return QStringLiteral("已跳过");
    }
    return QStringLiteral("未知");
}

QJsonArray stringListJson(const QStringList &values)
{
    QJsonArray array;
    for (const auto &value : values) {
        if (!value.trimmed().isEmpty()) {
            array.append(value);
        }
    }
    return array;
}

QString thumbnailUrl(const QString &kind, const QString &key, int frameNumber = 0)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("kind"), kind);
    query.addQueryItem(QStringLiteral("key"), key);
    if (frameNumber > 0) {
        query.addQueryItem(QStringLiteral("frame"), QString::number(frameNumber));
    }
    return QStringLiteral("/api/thumbnail?%1").arg(query.toString(QUrl::FullyEncoded));
}

QJsonObject assetJson(const GlobalVideoAsset &asset)
{
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("asset")},
        {QStringLiteral("key"), asset.videoKey},
        {QStringLiteral("fileName"), asset.fileName},
        {QStringLiteral("extension"), asset.extension},
        {QStringLiteral("assetType"), static_cast<int>(asset.assetType)},
        {QStringLiteral("assetTypeLabel"), Formatters::assetTypeLabel(asset.assetType)},
        {QStringLiteral("projectName"), asset.projectName},
        {QStringLiteral("sourceRootName"), asset.sourceRootName},
        {QStringLiteral("relativePath"), asset.relativePath},
        {QStringLiteral("absolutePath"), asset.absolutePath},
        {QStringLiteral("thumbnailPath"), asset.thumbnailPath},
        {QStringLiteral("thumbnailUrl"), thumbnailUrl(QStringLiteral("asset"), asset.videoKey)},
        {QStringLiteral("summary"), asset.summary},
        {QStringLiteral("technicalSummary"), asset.technicalSummary},
        {QStringLiteral("status"), statusText(asset.analysisStatus)},
        {QStringLiteral("modifiedAt"), asset.modifiedAt},
        {QStringLiteral("captureTime"), asset.captureTime},
        {QStringLiteral("captureDate"), asset.captureDate},
        {QStringLiteral("captureTimeSource"), asset.captureTimeSource},
        {QStringLiteral("captureTimeConfidence"), asset.captureTimeConfidence},
        {QStringLiteral("score"), asset.searchScore},
        {QStringLiteral("confidence"), asset.searchConfidence},
        {QStringLiteral("reasons"), stringListJson(asset.searchReasons)},
        {QStringLiteral("matchedFrameNumber"), asset.matchedFrameNumber},
        {QStringLiteral("matchedTimestampMs"), QString::number(asset.matchedTimestampMs)},
        {QStringLiteral("matchedFrameCaption"), asset.matchedFrameCaption},
    };
}

QJsonObject frameJson(const FrameSearchHit &frame)
{
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("frame")},
        {QStringLiteral("key"), frame.frameKey},
        {QStringLiteral("videoKey"), frame.videoKey},
        {QStringLiteral("fileName"), frame.fileName},
        {QStringLiteral("assetType"), static_cast<int>(frame.assetType)},
        {QStringLiteral("assetTypeLabel"), Formatters::assetTypeLabel(frame.assetType)},
        {QStringLiteral("projectName"), frame.projectName},
        {QStringLiteral("sourceRootName"), frame.sourceRootName},
        {QStringLiteral("relativePath"), frame.relativePath},
        {QStringLiteral("imagePath"), frame.imagePath},
        {QStringLiteral("thumbnailUrl"), thumbnailUrl(QStringLiteral("frame"), frame.videoKey, frame.frameNumber)},
        {QStringLiteral("frameNumber"), frame.frameNumber},
        {QStringLiteral("timestampMs"), QString::number(frame.timestampMs)},
        {QStringLiteral("caption"), frame.caption},
        {QStringLiteral("ocrText"), frame.ocrText},
        {QStringLiteral("tags"), stringListJson(frame.tags)},
        {QStringLiteral("objects"), stringListJson(frame.objects)},
        {QStringLiteral("score"), frame.score},
        {QStringLiteral("confidence"), frame.confidence},
        {QStringLiteral("reasons"), stringListJson(frame.reasons)},
    };
}

QJsonArray entityFactsJson(const QVector<VisionEntityFact> &entities)
{
    QJsonArray array;
    for (const auto &entity : entities) {
        array.append(QJsonObject{
            {QStringLiteral("category"), entity.category},
            {QStringLiteral("label"), entity.label},
            {QStringLiteral("colors"), stringListJson(entity.colors)},
            {QStringLiteral("materials"), stringListJson(entity.materials)},
            {QStringLiteral("attributes"), stringListJson(entity.attributes)},
        });
    }
    return array;
}

QJsonObject frameDetailJson(const FrameAnalysisRecord &frame)
{
    return QJsonObject{
        {QStringLiteral("id"), QString::number(frame.id)},
        {QStringLiteral("frameNumber"), frame.frameNumber},
        {QStringLiteral("timestampMs"), QString::number(frame.timestampMs)},
        {QStringLiteral("imageUrl"), thumbnailUrl(QStringLiteral("frame"), frame.videoKey, frame.frameNumber)},
        {QStringLiteral("caption"), frame.caption},
        {QStringLiteral("tags"), stringListJson(frame.tags)},
        {QStringLiteral("objects"), stringListJson(frame.objects)},
        {QStringLiteral("actions"), frame.actions},
        {QStringLiteral("setting"), frame.setting},
        {QStringLiteral("entities"), entityFactsJson(frame.entities)},
        {QStringLiteral("ocrText"), frame.ocrText},
        {QStringLiteral("ocrBlocks"), stringListJson(frame.ocrBlocks)},
        {QStringLiteral("factsComplete"), frame.factsComplete},
        {QStringLiteral("analysisState"), frameStatusText(frame.analysisState)},
        {QStringLiteral("analyzedAt"), frame.analyzedAt},
        {QStringLiteral("errorMessage"), frame.errorMessage},
    };
}

QJsonObject folderJson(const FolderSearchHit &folder)
{
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("folder")},
        {QStringLiteral("key"), folder.folderKey},
        {QStringLiteral("name"), folder.name},
        {QStringLiteral("projectName"), folder.projectName},
        {QStringLiteral("sourceRootName"), folder.sourceRootName},
        {QStringLiteral("relativePath"), folder.relativePath},
        {QStringLiteral("absolutePath"), folder.absolutePath},
        {QStringLiteral("recursiveFileCount"), QString::number(folder.recursiveFileCount)},
        {QStringLiteral("normalizedDate"), folder.normalizedDate},
        {QStringLiteral("score"), folder.score},
        {QStringLiteral("confidence"), folder.confidence},
        {QStringLiteral("reasons"), stringListJson(folder.reasons)},
    };
}

QJsonArray limitedResults(const MaterialSearchResult &result, qsizetype limit)
{
    QJsonArray items;
    const auto appendUntilLimit = [&](const auto &rows, auto toJson) {
        for (const auto &row : rows) {
            if (items.size() >= limit) {
                return;
            }
            items.append(toJson(row));
        }
    };
    appendUntilLimit(result.frames, frameJson);
    appendUntilLimit(result.assets, assetJson);
    appendUntilLimit(result.folders, folderJson);
    return items;
}

QString firstRequestLinePath(const QByteArray &requestBytes, QByteArray *method)
{
    const auto lineEnd = requestBytes.indexOf('\n');
    const auto firstLine = requestBytes.left(lineEnd >= 0 ? lineEnd : requestBytes.size()).trimmed();
    const auto parts = firstLine.split(' ');
    if (parts.size() < 2) {
        return {};
    }
    if (method) {
        *method = parts.at(0).trimmed().toUpper();
    }
    return QString::fromUtf8(parts.at(1));
}

qint64 queryDataGeneration(const QString &databasePath)
{
    const QFileInfo database(databasePath);
    const QFileInfo index(Paths::semanticSearchIndexPath());
    return database.lastModified().toMSecsSinceEpoch()
        ^ index.lastModified().toMSecsSinceEpoch()
        ^ database.size()
        ^ index.size();
}
}

WebSearchService::WebSearchService(MaterialCenterQueryService *queryService, QObject *parent)
    : QObject(parent)
    , m_queryService(queryService)
    , m_server(new QTcpServer(this))
{
    m_queryPool.setMaxThreadCount(2);
    m_queryPool.setExpiryTimeout(-1);
    connect(m_server, &QTcpServer::newConnection, this, &WebSearchService::handleNewConnection);
}

WebSearchService::~WebSearchService()
{
    stop();
    m_queryPool.clear();
    m_queryPool.waitForDone();
}

bool WebSearchService::start(quint16 preferredPort, QString *errorMessage)
{
    if (isRunning()) {
        return true;
    }

    const auto firstPort = std::max(preferredPort, kDefaultPort);
    for (quint16 candidate = firstPort; candidate <= kMaxPort; ++candidate) {
        if (m_server->listen(QHostAddress::AnyIPv4, candidate)) {
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Web 搜索服务启动失败：%1").arg(m_server->errorString());
    }
    return false;
}

void WebSearchService::stop()
{
    if (m_server) {
        m_server->close();
    }
    const auto sockets = findChildren<QTcpSocket *>();
    for (auto *socket : sockets) {
        if (socket) {
            socket->disconnectFromHost();
        }
    }
    m_requestBuffers.clear();
}

bool WebSearchService::isRunning() const
{
    return m_server && m_server->isListening();
}

quint16 WebSearchService::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QStringList WebSearchService::localAccessUrls() const
{
    QStringList urls;
    if (!isRunning()) {
        return urls;
    }

    urls.append(QStringLiteral("http://127.0.0.1:%1/").arg(port()));
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol
            || address.isLoopback()
            || address == QHostAddress::AnyIPv4) {
            continue;
        }
        const auto text = address.toString();
        if (!text.startsWith(QStringLiteral("169.254."))) {
            urls.append(QStringLiteral("http://%1:%2/").arg(text).arg(port()));
        }
    }
    urls.removeDuplicates();
    return urls;
}

void WebSearchService::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        auto *socket = m_server->nextPendingConnection();
        socket->setParent(this);
        if (findChildren<QTcpSocket *>(QString(), Qt::FindDirectChildrenOnly).size() > 64) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket->setReadBufferSize(kMaxRequestHeaderBytes + 1);
        m_requestBuffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_requestBuffers.remove(socket);
            socket->deleteLater();
        });
        QPointer<QTcpSocket> guardedSocket(socket);
        QTimer::singleShot(kRequestHeaderTimeoutMs, this, [this, guardedSocket]() {
            if (!guardedSocket || !m_requestBuffers.contains(guardedSocket)) {
                return;
            }
            sendResponse(guardedSocket,
                         408,
                         reasonPhrase(408),
                         "application/json; charset=utf-8",
                         QByteArrayLiteral("{\"error\":\"request_timeout\"}"));
        });
    }
}

void WebSearchService::handleReadyRead(QTcpSocket *socket)
{
    if (!socket || !m_requestBuffers.contains(socket)) {
        if (socket) socket->readAll();
        return;
    }
    auto &buffer = m_requestBuffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > kMaxRequestHeaderBytes) {
        socket->setProperty("requestTooLarge", true);
    }
    if (socket->property("requestTooLarge").toBool()) {
        const auto headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            buffer = buffer.right(3);
            return;
        }
        m_requestBuffers.remove(socket);
        sendResponse(socket,
                     413,
                     reasonPhrase(413),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"request_too_large\"}"));
        return;
    }
    const auto headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    const auto request = buffer.left(headerEnd + 4);
    m_requestBuffers.remove(socket);
    routeRequest(socket, request);
}

void WebSearchService::routeRequest(QTcpSocket *socket, const QByteArray &requestBytes)
{
    QByteArray method;
    const auto rawPath = firstRequestLinePath(requestBytes, &method);
    if (method != "GET") {
        sendResponse(socket,
                     405,
                     reasonPhrase(405),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"method_not_allowed\"}"));
        return;
    }
    if (rawPath.isEmpty()) {
        sendResponse(socket,
                     400,
                     reasonPhrase(400),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"bad_request\"}"));
        return;
    }

    const QUrl url(rawPath);
    const auto path = url.path();
    if (path == QStringLiteral("/") || path == QStringLiteral("/index.html")) {
        sendStaticFile(socket, QStringLiteral(":/websearch/index.html"), "text/html; charset=utf-8");
    } else if (path == QStringLiteral("/styles.css")) {
        sendStaticFile(socket, QStringLiteral(":/websearch/styles.css"), "text/css; charset=utf-8");
    } else if (path == QStringLiteral("/app.js")) {
        sendStaticFile(socket, QStringLiteral(":/websearch/app.js"), "application/javascript; charset=utf-8");
    } else if (path == QStringLiteral("/api/search")) {
        sendSearchResponse(socket, url);
    } else if (path == QStringLiteral("/api/video-detail")) {
        sendVideoDetailResponse(socket, url);
    } else if (path == QStringLiteral("/api/thumbnail")) {
        sendThumbnailResponse(socket, url);
    } else if (path == QStringLiteral("/api/health")) {
        sendHealthResponse(socket);
    } else {
        sendResponse(socket,
                     404,
                     reasonPhrase(404),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"not_found\"}"));
    }
}

void WebSearchService::runAsyncResponse(QTcpSocket *socket,
                                        std::function<AsyncResponse()> operation)
{
    if (!socket) {
        return;
    }
    if (m_inFlightQueries >= kMaxInFlightQueries) {
        sendResponse(socket,
                     503,
                     reasonPhrase(503),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"server_busy\"}"));
        return;
    }
    ++m_inFlightQueries;
    QPointer<QTcpSocket> guardedSocket(socket);
    auto *watcher = new QFutureWatcher<AsyncResponse>(this);
    connect(watcher, &QFutureWatcher<AsyncResponse>::finished, this,
            [this, watcher, guardedSocket]() {
        const auto response = watcher->result();
        watcher->deleteLater();
        m_inFlightQueries = qMax(0, m_inFlightQueries - 1);
        if (!guardedSocket) {
            return;
        }
        sendResponse(guardedSocket,
                     response.statusCode,
                     reasonPhrase(response.statusCode),
                     response.contentType,
                     response.body);
    });
    watcher->setFuture(QtConcurrent::run(&m_queryPool, std::move(operation)));
}

void WebSearchService::sendVideoDetailResponse(QTcpSocket *socket, const QUrl &url)
{
    if (!m_queryService) {
        sendResponse(socket,
                     503,
                     reasonPhrase(503),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"search_service_unavailable\"}"));
        return;
    }

    const QUrlQuery query(url);
    const auto key = query.queryItemValue(QStringLiteral("key"), QUrl::FullyDecoded).trimmed();
    if (key.isEmpty()) {
        sendResponse(socket,
                     400,
                     reasonPhrase(400),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"invalid_video_key\"}"));
        return;
    }
    const auto requestedFrameLimit = query.queryItemValue(QStringLiteral("limit")).toInt();
    const auto frameLimit = qBound(1,
                                   requestedFrameLimit > 0
                                       ? requestedFrameLimit
                                       : static_cast<int>(kDefaultLimit),
                                   static_cast<int>(kMaxLimit));
    const auto afterFrame = qMax(0, query.queryItemValue(QStringLiteral("afterFrame")).toInt());
    const auto databasePath = m_queryService->databaseFilePath();
    const auto generation = queryDataGeneration(databasePath);
    runAsyncResponse(socket, [databasePath, generation, key, frameLimit, afterFrame]() {
        AsyncResponse response;
        QString errorMessage;
        auto *context = materialCenterReadContextForCurrentThread(
            databasePath, generation, &errorMessage);
        if (!context || !context->queryService()) {
            response.statusCode = 503;
            response.body = QByteArrayLiteral("{\"error\":\"search_service_unavailable\"}");
            return response;
        }
        const auto page = context->queryService()->fetchDetailPage(
            key, frameLimit, afterFrame);
        const auto &detail = page.detail;
        if (detail.asset.videoKey.trimmed().isEmpty()) {
            response.statusCode = 404;
            response.body = QByteArrayLiteral("{\"error\":\"video_not_found\"}");
            return response;
        }

        QJsonObject asset = assetJson(detail.asset);
        asset.insert(QStringLiteral("sizeBytes"), QString::number(detail.asset.sizeBytes));
        asset.insert(QStringLiteral("durationMs"), QString::number(detail.asset.durationMs));
        const QJsonObject plan{
            {QStringLiteral("samplingPolicy"), detail.visualAnalysisPlan.samplingPolicy},
            {QStringLiteral("frameInterval"), detail.visualAnalysisPlan.frameInterval},
            {QStringLiteral("sourceFrameCount"), detail.visualAnalysisPlan.sourceFrameCount},
            {QStringLiteral("plannedFrameCount"), detail.visualAnalysisPlan.plannedFrameCount},
            {QStringLiteral("updatedAt"), detail.visualAnalysisPlan.updatedAt}
        };
        const QJsonObject task{
            {QStringLiteral("stage"), static_cast<int>(detail.asset.analysisTask.stage)},
            {QStringLiteral("totalFrames"), detail.asset.analysisTask.totalFrames},
            {QStringLiteral("completedFrames"), detail.asset.analysisTask.completedFrames},
            {QStringLiteral("successfulFrames"), detail.asset.analysisTask.successfulFrames},
            {QStringLiteral("skippedFrames"), detail.asset.analysisTask.skippedFrames},
            {QStringLiteral("lastErrorMessage"), detail.asset.analysisTask.lastErrorMessage},
            {QStringLiteral("lastUpdatedAt"), detail.asset.analysisTask.lastUpdatedAt}
        };
        QJsonArray frames;
        for (const auto &frame : detail.frames) {
            frames.append(frameDetailJson(frame));
        }
        QJsonArray dimensions;
        for (const auto &dimension : detail.dimensionAnalyses) {
            dimensions.append(QJsonObject{
                {QStringLiteral("name"), dimension.name},
                {QStringLiteral("detail"), dimension.detail},
                {QStringLiteral("analyzedAt"), dimension.analyzedAt}
            });
        }
        response.statusCode = 200;
        response.body = QJsonDocument(QJsonObject{
            {QStringLiteral("asset"), asset},
            {QStringLiteral("hasVisualAnalysisPlan"), detail.hasVisualAnalysisPlan},
            {QStringLiteral("visualAnalysisPlan"), plan},
            {QStringLiteral("analysisTask"), task},
            {QStringLiteral("frames"), frames},
            {QStringLiteral("frameTotal"), page.totalFrameCount},
            {QStringLiteral("nextFrame"), page.nextFrameNumber},
            {QStringLiteral("hasMoreFrames"), page.hasMoreFrames},
            {QStringLiteral("dimensions"), dimensions}
        }).toJson(QJsonDocument::Compact);
        return response;
    });
}

void WebSearchService::sendStaticFile(QTcpSocket *socket,
                                      const QString &resourcePath,
                                      const QByteArray &contentType) const
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket,
                     404,
                     reasonPhrase(404),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"resource_missing\"}"));
        return;
    }
    sendResponse(socket, 200, reasonPhrase(200), contentType, file.readAll());
}

void WebSearchService::sendSearchResponse(QTcpSocket *socket, const QUrl &url)
{
    if (!m_queryService) {
        sendResponse(socket,
                     503,
                     reasonPhrase(503),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"search_service_unavailable\"}"));
        return;
    }

    const QUrlQuery query(url);
    const auto text = query.queryItemValue(QStringLiteral("q")).trimmed();
    const auto requestedLimit = query.queryItemValue(QStringLiteral("limit")).toLongLong();
    const auto limit = qBound<qsizetype>(
        1,
        requestedLimit > 0 ? requestedLimit : kDefaultLimit,
        kMaxLimit);

    MaterialSearchScope scope;
    scope.limit = limit;
    const auto databasePath = m_queryService->databaseFilePath();
    const auto generation = queryDataGeneration(databasePath);
    runAsyncResponse(socket, [databasePath, generation, text, scope, limit]() {
        AsyncResponse response;
        QString errorMessage;
        auto *context = materialCenterReadContextForCurrentThread(
            databasePath, generation, &errorMessage);
        if (!context || !context->queryService()) {
            response.statusCode = 503;
            response.body = QJsonDocument(QJsonObject{
                {QStringLiteral("error"), QStringLiteral("search_service_unavailable")},
                {QStringLiteral("detail"), errorMessage}
            }).toJson(QJsonDocument::Compact);
            return response;
        }
        const auto result = context->queryService()->searchMaterials(text, scope);
        const auto total = result.frames.size() + result.assets.size() + result.folders.size();
        response.statusCode = 200;
        response.body = QJsonDocument(QJsonObject{
            {QStringLiteral("query"), text},
            {QStringLiteral("total"), total},
            {QStringLiteral("returned"), qMin<int>(static_cast<int>(limit), total)},
            {QStringLiteral("semanticSearchAvailable"), result.semanticSearchAvailable},
            {QStringLiteral("warning"), result.warningMessage},
            {QStringLiteral("interpretation"), stringListJson(result.parsedQuery.interpretationLabels)},
            {QStringLiteral("excludedPartialCount"), result.excludedPartialCount},
            {QStringLiteral("results"), limitedResults(result, limit)}
        }).toJson(QJsonDocument::Compact);
        return response;
    });
}

void WebSearchService::sendThumbnailResponse(QTcpSocket *socket, const QUrl &url)
{
    if (!m_queryService) {
        sendResponse(socket,
                     503,
                     reasonPhrase(503),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"search_service_unavailable\"}"));
        return;
    }

    const QUrlQuery query(url);
    const auto kind = query.queryItemValue(QStringLiteral("kind"), QUrl::FullyDecoded).trimmed();
    const auto key = query.queryItemValue(QStringLiteral("key"), QUrl::FullyDecoded).trimmed();
    const auto frameNumber = query.queryItemValue(QStringLiteral("frame"), QUrl::FullyDecoded).toInt();
    if (key.isEmpty() || (kind != QStringLiteral("asset") && kind != QStringLiteral("frame"))) {
        sendResponse(socket,
                     400,
                     reasonPhrase(400),
                     "application/json; charset=utf-8",
                     QByteArrayLiteral("{\"error\":\"invalid_thumbnail_request\"}"));
        return;
    }

    const auto databasePath = m_queryService->databaseFilePath();
    const auto generation = queryDataGeneration(databasePath);
    runAsyncResponse(socket, [databasePath, generation, kind, key, frameNumber]() {
        AsyncResponse response;
        QString errorMessage;
        auto *context = materialCenterReadContextForCurrentThread(
            databasePath, generation, &errorMessage);
        if (!context || !context->queryService()) {
            response.statusCode = 503;
            response.body = QByteArrayLiteral("{\"error\":\"search_service_unavailable\"}");
            return response;
        }
        const auto page = context->queryService()->fetchDetailPage(
            key, 1, 0, kind == QStringLiteral("frame") ? frameNumber : -1);
        const auto &detail = page.detail;
        QString imagePath;
        if (kind == QStringLiteral("asset")) {
            imagePath = detail.asset.thumbnailPath;
            if (imagePath.trimmed().isEmpty()
                && detail.asset.assetType == AssetType::Image) {
                imagePath = detail.asset.absolutePath;
            }
        } else if (frameNumber > 0) {
            for (const auto &frame : detail.frames) {
                if (frame.frameNumber == frameNumber) {
                    imagePath = frame.imagePath;
                    break;
                }
            }
        }

        const QFileInfo fileInfo(imagePath);
        if (imagePath.trimmed().isEmpty() || !fileInfo.isFile()) {
            response.statusCode = 404;
            response.body = QByteArrayLiteral("{\"error\":\"thumbnail_not_found\"}");
            return response;
        }
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            response.statusCode = 404;
            response.body = QByteArrayLiteral("{\"error\":\"thumbnail_unreadable\"}");
            return response;
        }
        const auto mimeType = QMimeDatabase().mimeTypeForFile(
            fileInfo, QMimeDatabase::MatchExtension);
        response.statusCode = 200;
        response.contentType = mimeType.isValid()
            ? mimeType.name().toUtf8()
            : QByteArrayLiteral("application/octet-stream");
        response.body = file.readAll();
        return response;
    });
}

void WebSearchService::sendHealthResponse(QTcpSocket *socket) const
{
    const QJsonObject body{
        {QStringLiteral("ok"), isRunning()},
        {QStringLiteral("port"), port()},
        {QStringLiteral("urls"), stringListJson(localAccessUrls())},
    };
    sendJson(socket, 200, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void WebSearchService::sendJson(QTcpSocket *socket, int statusCode, const QByteArray &body) const
{
    sendResponse(socket,
                 statusCode,
                 reasonPhrase(statusCode),
                 "application/json; charset=utf-8",
                 body);
}

void WebSearchService::sendResponse(QTcpSocket *socket,
                                    int statusCode,
                                    const QByteArray &reason,
                                    const QByteArray &contentType,
                                    const QByteArray &body) const
{
    if (!socket) {
        return;
    }
    if (socket->property("responseSent").toBool()) return;
    socket->setProperty("responseSent", true);
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    QPointer<QTcpSocket> guardedSocket(socket);
    QObject::connect(socket,
                     &QTcpSocket::bytesWritten,
                     socket,
                     [guardedSocket](qint64) {
                         if (guardedSocket && guardedSocket->bytesToWrite() == 0) {
                             guardedSocket->disconnectFromHost();
                         }
                     });
    socket->write(response);
    socket->flush();
    QTimer::singleShot(1000, socket, [guardedSocket]() {
        if (guardedSocket) {
            guardedSocket->disconnectFromHost();
        }
    });
}
