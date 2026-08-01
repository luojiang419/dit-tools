#include "application/MaterialCenterQueryService.h"
#include "application/websearch/WebSearchService.h"
#include "core/search/SearchEngine.h"
#include "domain/Enums.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QElapsedTimer>

namespace {
bool execSql(QSqlDatabase db, const QString &sql, QString *errorMessage = nullptr)
{
    QSqlQuery query(db);
    if (query.exec(sql)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

QString globalDatabasePath()
{
    return QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("material-center.sqlite"));
}

class GlobalDbFixture {
public:
    GlobalDbFixture()
    {
        QFile::remove(globalDatabasePath());
        thumbnailPath = QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("web-search-thumbnail.png"));
        QFile::remove(thumbnailPath);
        if (!manager.openDatabase(&errorMessage)) {
            return;
        }
        valid = seed();
    }

    ~GlobalDbFixture()
    {
        manager.closeDatabase();
        QFile::remove(globalDatabasePath());
        QFile::remove(thumbnailPath);
    }

    GlobalDatabaseManager manager;
    bool valid = false;
    QString errorMessage;
    QString thumbnailPath;

private:
    bool seed()
    {
        auto db = manager.database();
        QFile thumbnail(thumbnailPath);
        if (!thumbnail.open(QIODevice::WriteOnly)
            || thumbnail.write(QByteArray::fromBase64(
                   "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=")) <= 0) {
            errorMessage = QStringLiteral("无法创建缩略图测试文件");
            return false;
        }
        thumbnail.close();
        if (!execSql(db,
                     QStringLiteral("INSERT INTO project_registry "
                                    "(project_uuid, project_name, project_database_path, last_synced_at, sync_status, error_message) "
                                    "VALUES ('project-web', 'Web Project', 'G:/projects/web/project.cinevault.sqlite', "
                                    "'2026-07-30T10:00:00', 'success', '')"),
                     &errorMessage)) {
            return false;
        }

        QSqlQuery asset(db);
        asset.prepare(QStringLiteral(
            "INSERT INTO global_video_asset "
            "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
            "asset_id, file_name, extension, absolute_path, relative_path, asset_type, size_bytes, modified_at, duration_ms, "
            "thumbnail_path, thumbnail_status, analysis_status, confirmation_status, technical_summary, source_text, "
            "error_message, last_synced_at, updated_at) "
            "VALUES ('web-doc-1:488', 'project-web', 'Web Project', 'G:/projects/web/project.cinevault.sqlite', 3, '素材库', "
            "101, 'license-notes.md', 'md', 'G:/projects/web/docs/license-notes.md', 'docs/license-notes.md', ?, "
                                    "2048, '2026-07-30T10:00:00', 0, ?, 1, ?, 0, 'Markdown 文档', '内部授权检索样例', '', "
                                    "'2026-07-30T10:00:00', '2026-07-30T10:00:00')"));
        asset.addBindValue(static_cast<int>(AssetType::Document));
        asset.addBindValue(thumbnailPath);
        asset.addBindValue(static_cast<int>(VideoAnalysisStatus::Ready));
        if (!asset.exec()) {
            errorMessage = asset.lastError().text();
            return false;
        }
        if (!execSql(db,
                     QStringLiteral(
                         "UPDATE global_video_asset SET capture_time = '2025-11-20T14:00:00+08:00', "
                         "capture_date = '2025-11-20', capture_time_source = 'quicktime_creation_date', "
                         "capture_time_confidence = 1.0 WHERE video_key = 'web-doc-1:488'"),
                     &errorMessage)) {
            return false;
        }

        QSqlQuery result(db);
        result.prepare(QStringLiteral(
            "INSERT INTO video_analysis_result "
            "(video_key, summary, keywords_json, scenes_json, search_text, model_name, prompt_version, analyzed_at, confirmed_at) "
            "VALUES ('web-doc-1:488', '授权说明摘要', '[\"授权\"]', '[\"文档\"]', '授权说明摘要 内部授权检索样例', "
            "'test-model', 'test', '2026-07-30T10:05:00', '')"));
        if (!result.exec()) {
            errorMessage = result.lastError().text();
            return false;
        }

        QSqlQuery frame(db);
        frame.prepare(QStringLiteral(
            "INSERT INTO video_frame_analysis "
            "(video_key, frame_number, timestamp_ms, image_path, caption, tags_json, objects_json, "
            "actions, setting_text, entities_json, ocr_text, facts_complete, analyzed_at, analysis_state) "
            "VALUES ('web-doc-1:488', 3, 10400, ?, '画面中出现授权说明标题', '[\"授权\"]', '[\"标题\"]', "
            "'镜头保持稳定', '文档页面', '[]', 'LICENSE', 1, '2026-07-30T10:06:00', 1)"));
        frame.addBindValue(thumbnailPath);
        if (!frame.exec()) {
            errorMessage = frame.lastError().text();
            return false;
        }

        if (manager.hasFts5()) {
            QSqlQuery fts(db);
            fts.prepare(QStringLiteral(
                "INSERT INTO video_search_fts "
                "(video_key, project_name, source_root_name, file_name, relative_path, absolute_path, asset_type_label, "
                "extension, technical_summary, summary, keywords, captions, source_text) "
                "VALUES ('web-doc-1:488', 'Web Project', '素材库', 'license-notes.md', 'docs/license-notes.md', "
                "'G:/projects/web/docs/license-notes.md', '文档', 'md', 'Markdown 文档', "
                "'授权说明摘要', '授权', '', '内部授权检索样例')"));
            if (!fts.exec()) {
                errorMessage = fts.lastError().text();
                return false;
            }
        }
        return true;
    }
};

QByteArray httpGet(quint16 port, const QByteArray &path)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    QElapsedTimer timer;
    timer.start();
    while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    if (socket.state() != QAbstractSocket::ConnectedState) return {};

    socket.write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");

    QByteArray response;
    while (timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        response += socket.readAll();
        if (response.contains("\r\n\r\n")) {
            const auto delimiter = response.indexOf("\r\n\r\n");
            const auto header = response.left(delimiter);
            const auto contentLengthPrefix = QByteArrayLiteral("Content-Length: ");
            const auto lengthIndex = header.indexOf(contentLengthPrefix);
            if (lengthIndex >= 0) {
                const auto lengthStart = lengthIndex + contentLengthPrefix.size();
                const auto lengthEnd = header.indexOf('\r', lengthStart);
                const auto expectedLength = header.mid(lengthStart, lengthEnd - lengthStart).toLongLong();
                if (response.size() >= delimiter + 4 + expectedLength) {
                    break;
                }
            }
        }
        QTest::qWait(1);
    }
    response += socket.readAll();
    return response;
}

QByteArray responseBody(const QByteArray &response)
{
    const auto delimiter = response.indexOf("\r\n\r\n");
    return delimiter < 0 ? QByteArray{} : response.mid(delimiter + 4);
}
}

class WebSearchServiceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("DITToolsTests"));
        QCoreApplication::setApplicationName(QStringLiteral("WebSearchServiceTest"));
    }

    void healthEndpoint_returnsAccessUrls()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService queryService(&fixture.manager, &searchEngine);
        WebSearchService service(&queryService);

        QString errorMessage;
        QVERIFY2(service.start(17890, &errorMessage), qPrintable(errorMessage));

        const auto response = httpGet(service.port(), "/api/health");
        QVERIFY(response.startsWith("HTTP/1.1 200 OK"));
        const auto document = QJsonDocument::fromJson(responseBody(response));
        QVERIFY(document.isObject());
        QVERIFY(document.object().value(QStringLiteral("ok")).toBool());
        QVERIFY(document.object().value(QStringLiteral("port")).toInt() > 0);
    }

    void searchEndpoint_returnsMaterialResults()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService queryService(&fixture.manager, &searchEngine);
        WebSearchService service(&queryService);
        const auto detail = queryService.fetchDetail(QStringLiteral("web-doc-1:488"));
        QVERIFY2(QFile::exists(fixture.thumbnailPath), qPrintable(fixture.thumbnailPath));
        QCOMPARE(detail.asset.thumbnailPath, fixture.thumbnailPath);

        QString errorMessage;
        QVERIFY2(service.start(17890, &errorMessage), qPrintable(errorMessage));

        const auto response = httpGet(service.port(), "/api/search?q=%E6%8E%88%E6%9D%83&limit=10");
        QVERIFY(response.startsWith("HTTP/1.1 200 OK"));
        const auto document = QJsonDocument::fromJson(responseBody(response));
        QVERIFY(document.isObject());
        const auto body = document.object();
        QCOMPARE(body.value(QStringLiteral("query")).toString(), QStringLiteral("授权"));
        QVERIFY(body.value(QStringLiteral("total")).toInt() >= 1);
        const auto results = body.value(QStringLiteral("results")).toArray();
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().toObject().value(QStringLiteral("fileName")).toString(),
                 QStringLiteral("license-notes.md"));
        QCOMPARE(results.first().toObject().value(QStringLiteral("captureDate")).toString(),
                 QStringLiteral("2025-11-20"));
        QCOMPARE(results.first().toObject().value(QStringLiteral("captureTimeSource")).toString(),
                 QStringLiteral("quicktime_creation_date"));
        QVERIFY(results.first().toObject().value(QStringLiteral("thumbnailUrl")).toString().contains(
            QStringLiteral("/api/thumbnail")));

        const auto thumbnailResponse = httpGet(service.port(), "/api/thumbnail?kind=asset&key=web-doc-1%3A488");
        QVERIFY2(thumbnailResponse.startsWith("HTTP/1.1 200 OK"), qPrintable(thumbnailResponse));
        QVERIFY(thumbnailResponse.contains("Content-Type: image/png"));
        QVERIFY(responseBody(thumbnailResponse).size() > 20);

        const auto detailResponse = httpGet(service.port(), "/api/video-detail?key=web-doc-1%3A488");
        QVERIFY2(detailResponse.startsWith("HTTP/1.1 200 OK"), qPrintable(detailResponse));
        const auto detailDocument = QJsonDocument::fromJson(responseBody(detailResponse));
        QVERIFY(detailDocument.isObject());
        const auto detailBody = detailDocument.object();
        QCOMPARE(detailBody.value(QStringLiteral("asset")).toObject().value(QStringLiteral("fileName")).toString(),
                 QStringLiteral("license-notes.md"));
        const auto frames = detailBody.value(QStringLiteral("frames")).toArray();
        QVERIFY(!frames.isEmpty());
        QCOMPARE(frames.first().toObject().value(QStringLiteral("frameNumber")).toInt(), 3);
        QCOMPARE(frames.first().toObject().value(QStringLiteral("caption")).toString(),
                 QStringLiteral("画面中出现授权说明标题"));
        QVERIFY(frames.first().toObject().value(QStringLiteral("imageUrl")).toString().contains(
            QStringLiteral("/api/thumbnail")));
        QVERIFY(detailBody.value(QStringLiteral("visualAnalysisPlan")).isObject());
    }
};

QTEST_GUILESS_MAIN(WebSearchServiceTest)

#include "WebSearchServiceTest.moc"
