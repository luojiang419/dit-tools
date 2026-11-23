#include <QtTest>

#include "infrastructure/search/SearchAssistantClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>

namespace {
qsizetype contentLengthFromHeader(const QByteArray &header)
{
    for (auto line : header.split('\n')) {
        line = line.trimmed();
        const QByteArray prefix("content-length:");
        if (!line.toLower().startsWith(prefix)) {
            continue;
        }
        bool ok = false;
        const auto length = line.mid(prefix.size()).trimmed().toLongLong(&ok);
        return ok ? static_cast<qsizetype>(length) : 0;
    }
    return 0;
}

QByteArray chatResponse(const QString &content, const QString &finishReason)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("choices"), QJsonArray{QJsonObject{
            {QStringLiteral("finish_reason"), finishReason},
            {QStringLiteral("message"), QJsonObject{
                {QStringLiteral("content"), content}
            }}
        }}}
    }).toJson(QJsonDocument::Compact);
}

QJsonObject validPlan()
{
    return QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("result_target"), QStringLiteral("assets")},
        {QStringLiteral("semantic_variants"), QJsonArray{QJsonObject{
            {QStringLiteral("text"), QStringLiteral("红色牛仔裤")},
            {QStringLiteral("weight"), 0.85}
        }}},
        {QStringLiteral("lexical_groups"), QJsonArray{QJsonObject{
            {QStringLiteral("mode"), QStringLiteral("required")},
            {QStringLiteral("alternatives"), QJsonArray{
                QStringLiteral("牛仔裤"), QStringLiteral("丹宁裤")
            }}
        }}},
        {QStringLiteral("asset_types"), QJsonArray{}},
        {QStringLiteral("folder_by_asset_criteria"), false},
        {QStringLiteral("ocr_text"), QString()},
        {QStringLiteral("entities"), QJsonArray{QJsonObject{
            {QStringLiteral("label"), QStringLiteral("牛仔裤")},
            {QStringLiteral("colors"), QJsonArray{QStringLiteral("红色")}},
            {QStringLiteral("materials"), QJsonArray{}},
            {QStringLiteral("attributes"), QJsonArray{}}
        }}},
        {QStringLiteral("cooccurrence"), QStringLiteral("none")},
        {QStringLiteral("confidence"), 0.93},
        {QStringLiteral("ambiguities"), QJsonArray{}},
        {QStringLiteral("explanation"), QStringLiteral("按颜色和对象搜索")}
    };
}

void installSequentialResponder(QTcpServer *server,
                                QVector<QByteArray> *capturedBodies,
                                const QVector<QByteArray> &responses)
{
    const auto sharedResponses = QSharedPointer<QVector<QByteArray>>::create(responses);
    const auto nextIndex = QSharedPointer<int>::create(0);
    QObject::connect(server,
                     &QTcpServer::newConnection,
                     server,
                     [server, capturedBodies, sharedResponses, nextIndex]() {
        auto *socket = server->nextPendingConnection();
        QObject::connect(socket,
                         &QTcpSocket::readyRead,
                         socket,
                         [socket,
                          capturedBodies,
                          sharedResponses,
                          nextIndex,
                          request = QByteArray()]() mutable {
            request.append(socket->readAll());
            const auto headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const auto bodyStart = headerEnd + 4;
            const auto expectedLength = contentLengthFromHeader(request.left(headerEnd));
            if (request.size() < bodyStart + expectedLength) {
                return;
            }

            capturedBodies->append(request.mid(bodyStart, expectedLength));
            const auto responseIndex = qMin(*nextIndex,
                                            static_cast<int>(sharedResponses->size()) - 1);
            ++(*nextIndex);
            const auto responseBody = sharedResponses->at(responseIndex);
            socket->write(QByteArrayLiteral(
                              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                          + QByteArray::number(responseBody.size())
                          + QByteArrayLiteral("\r\nConnection: close\r\n\r\n"));
            socket->write(responseBody);
            socket->disconnectFromHost();
        });
    });
}
}

class SearchAssistantClientRecoveryTest : public QObject {
    Q_OBJECT

private slots:
    void repairsTruncatedJsonExactlyOnce()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QVector<QByteArray> requests;
        const auto repairedContent = QString::fromUtf8(
            QJsonDocument(validPlan()).toJson(QJsonDocument::Compact));
        installSequentialResponder(
            &server,
            &requests,
            {chatResponse(QStringLiteral("{\"version\":2,\"result_target\":\"assets\""),
                          QStringLiteral("length")),
             chatResponse(repairedContent, QStringLiteral("stop"))});

        SearchAssistantClient client;
        QString error;
        int statusCode = 0;
        const auto result = client.understandQuery(
            QStringLiteral("搜索红色牛仔裤"),
            QDate(2026, 7, 17),
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()),
            QStringLiteral("test-model"),
            5,
            &error,
            &statusCode);

        QVERIFY2(result.has_value(), qPrintable(error));
        QCOMPARE(statusCode, 200);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(result->strictEntities.size(), 1);
        QCOMPARE(result->strictEntities.first().label, QStringLiteral("牛仔裤"));
        QCOMPARE(result->strictEntities.first().colors, QStringList{QStringLiteral("红色")});

        const auto firstRequest = QJsonDocument::fromJson(requests.at(0)).object();
        const auto repairRequest = QJsonDocument::fromJson(requests.at(1)).object();
        QCOMPARE(firstRequest.value(QStringLiteral("max_tokens")).toInt(), 768);
        QCOMPARE(repairRequest.value(QStringLiteral("max_tokens")).toInt(), 768);
        const auto repairMessages = repairRequest.value(QStringLiteral("messages")).toArray();
        QCOMPARE(repairMessages.size(), 2);
        QVERIFY(repairMessages.first().toObject()
                    .value(QStringLiteral("content"))
                    .toString()
                    .contains(QStringLiteral("JSON 修复器")));
    }
};

QTEST_GUILESS_MAIN(SearchAssistantClientRecoveryTest)

#include "SearchAssistantClientRecoveryTest.moc"
